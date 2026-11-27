/* Fixture recorder CLI: ifx_record <name> <path>, where <name> is an effect,
 * "tables", or --list. The firmware harness is linked in; nothing else here
 * knows about it. */
#include "ifx_dsp_board.h"
#include "ifx_dsp_math.h"
#include "ifx_effect.h"
#include "ifx_emu.h"
#include "ifx_firmware.h"
#include "ifx_fixture.h"
#include "ifx_signal.h"
#include "ifx_tables.h"

#include "hatari/dsp_cpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const char *name;
  uint32_t address;
  uint32_t words;
} table_source;

static const table_source k_tables[] = {
    {"ifx_level_table", 0x07BEu, IFX_LEVEL_TABLE_WORDS},
    {"ifx_pan_table", 0x07F9u, IFX_PAN_TABLE_WORDS},
    {"ifx_hush_threshold_table", 0x085Eu, IFX_HUSH_THRESHOLD_TABLE_WORDS},
    {"ifx_hush_slope_table", 0x0877u, IFX_HUSH_SLOPE_TABLE_WORDS},
    {"ifx_hush_release_table", 0x0890u, IFX_HUSH_RELEASE_TABLE_WORDS},
    {"ifx_ducker_release_table", 0x089Au, IFX_DUCKER_RELEASE_TABLE_WORDS},
    {"ifx_gate_decay_table", 0x08AEu, IFX_GATE_DECAY_TABLE_WORDS},
    {"ifx_gate_hold_table", 0x08CEu, IFX_GATE_HOLD_TABLE_WORDS},
    {"ifx_coarse_pitch_table", 0x0932u, IFX_COARSE_PITCH_TABLE_WORDS},
    {"ifx_fine_pitch_table", 0x09E7u, IFX_FINE_PITCH_TABLE_WORDS},
    {"ifx_reverb_type_table", 0x0172u, IFX_REVERB_TYPE_COUNT * IFX_REVERB_TYPE_TABLE_WORDS},
};

#define TABLE_COUNT (sizeof k_tables / sizeof k_tables[0])
#define PRESET_FIRST 81u
#define PRESET_LAST 160u

static int add_table(ifx_fixture_writer *writer, const char *name, uint32_t *first,
                     const int32_t *words, uint32_t count) {
  ifx_fixture_table index;
  unsigned char record[IFX_FIX_TABLE_WORDS_SIZE];
  memset(&index, 0, sizeof index);
  snprintf(index.name, sizeof index.name, "%s", name);
  index.first = *first;
  index.count = count;
  for (uint32_t word = 0; word < count; word++) {
    ifx_fixture_put_word(record, words[word]);
    if (ifx_fixture_writer_add(writer, IFX_FIX_TABLE_WORDS, sizeof record, record) != 0) {
      return -1;
    }
    (*first)++;
  }
  unsigned char bytes[IFX_FIX_TABLE_INDEX_SIZE];
  ifx_fixture_put_table(bytes, &index);
  return ifx_fixture_writer_add(writer, IFX_FIX_TABLE_INDEX, sizeof bytes, bytes);
}

static int record_tables(ifx_fixture_writer *writer, const ifx_emu *emu) {
  const ifx_dsp_board *dsp = ifx_emu_dsp(emu);
  uint32_t first = 0;
  int32_t *words = malloc(IFX_REVERB_TYPE_COUNT * IFX_REVERB_TYPE_TABLE_WORDS * sizeof *words);
  if (words == NULL) {
    return -1;
  }
  for (unsigned table = 0; table < TABLE_COUNT; table++) {
    const table_source *source = &k_tables[table];
    for (uint32_t word = 0; word < source->words; word++) {
      words[word] = ifx_s24(ifx_dsp_board_word(dsp, DSP_SPACE_P, source->address + word));
    }
    if (add_table(writer, source->name, &first, words, source->words) != 0) {
      free(words);
      return -1;
    }
  }
  free(words);

  const uint8_t *eprom = ifx_emu_eprom(emu);
  int32_t window[IFX_EMU_PARAMETER_COUNT];
  for (unsigned i = 0; i < IFX_EMU_PARAMETER_COUNT; i++) {
    window[i] = eprom[IFX_EMU_WINDOW_TABLE + i];
  }
  return add_table(writer, "mcu_window_table", &first, window, IFX_EMU_PARAMETER_COUNT);
}

static int vector_present(const int32_t *vectors, unsigned count, unsigned knobs,
                          const int32_t *candidate) {
  for (unsigned vector = 0; vector < count; vector++) {
    unsigned knob = 0;
    while (knob < knobs && vectors[vector * knobs + knob] == candidate[knob]) {
      knob++;
    }
    if (knob == knobs) {
      return 1;
    }
  }
  return 0;
}

static unsigned setup_vectors(const ifx_effect *effect, int32_t *vectors, unsigned cap) {
  unsigned knobs = effect->knobs;
  if (knobs == 0u || knobs > 64u) {
    return 0;
  }
  int32_t candidate[64];
  int32_t target[64] = {0};
  for (unsigned knob = 0; knob < knobs; knob++) {
    candidate[knob] = effect->knob[knob].def;
  }
  memcpy(vectors, candidate, knobs * sizeof *candidate);
  unsigned count = 1;
  for (unsigned knob = 0; knob < knobs; knob++) {
    const ifx_effect_knob *record = &effect->knob[knob];
    int span = record->max - record->min;
    unsigned positions = span + 1 <= 32 ? (unsigned)(span + 1) : 32u;
    for (unsigned i = 0; i < positions && count < cap; i++) {
      int value = span + 1 <= 32 ? record->min + (int)i
                                 : record->min + (int)((i * (unsigned)span + 15u) / 31u);
      candidate[knob] = value;
      if (!vector_present(vectors, count, knobs, candidate)) {
        memcpy(vectors + count * knobs, candidate, knobs * sizeof *candidate);
        count++;
      }
    }
    candidate[knob] = record->def;
    if (count < cap && !vector_present(vectors, count, knobs, candidate)) {
      memcpy(vectors + count * knobs, candidate, knobs * sizeof *candidate);
      count++;
    }
  }
  for (unsigned preset = 0; preset < 4u; preset++) {
    for (unsigned knob = 0; knob < knobs; knob++) {
      const ifx_effect_knob *record = &effect->knob[knob];
      if (preset == 0u) {
        target[knob] = ifx_knob_at(record, 1u, 3u);
      } else if (preset == 1u) {
        target[knob] = ifx_knob_at(record, 2u, 3u);
      } else if (preset == 2u) {
        target[knob] = record->min;
      } else {
        target[knob] = record->max;
      }
    }
    for (unsigned step = knobs; step-- > 0u;) {
      const unsigned knob = step;
      candidate[knob] = target[knob];
      if (count < cap && !vector_present(vectors, count, knobs, candidate)) {
        memcpy(vectors + count * knobs, candidate, knobs * sizeof *candidate);
        count++;
      }
    }
    for (unsigned knob = 0; knob < knobs; knob++) {
      const ifx_effect_knob *record = &effect->knob[knob];
      const int points[6] = {record->min,
                             ifx_knob_at(record, 1u, 3u),
                             ifx_knob_at(record, 2u, 3u),
                             record->max,
                             record->def,
                             target[knob]};
      for (unsigned point = 0; point < 6u; point++) {
        candidate[knob] = points[point];
        if (count < cap && !vector_present(vectors, count, knobs, candidate)) {
          memcpy(vectors + count * knobs, candidate, knobs * sizeof *candidate);
          count++;
        }
      }
      candidate[knob] = target[knob];
    }
  }
  return count;
}

/* One factory preset of the effect's configuration, as parameter bytes and as
 * the knob table's values. */
static int preset_values(const ifx_effect *effect, unsigned config, const ifx_emu *emu,
                         unsigned preset, int32_t *values, uint8_t bytes[IFX_EMU_PARAMETER_COUNT]) {
  unsigned preset_config = 0;
  if (!ifx_emu_factory_preset(emu, preset, &preset_config, bytes) || preset_config != config) {
    return -1;
  }
  for (unsigned knob = 0; knob < effect->knobs; knob++) {
    values[knob] = bytes[effect->knob[knob].param];
  }
  return 0;
}

static int record_effect(const ifx_firmware *adapter, const char *path) {
  const ifx_effect *effect = adapter->effect;
  unsigned knobs = effect->knobs;
  unsigned setup_count = 0;
  unsigned segments = 0;
  unsigned long frames = 0;
  unsigned char *record = NULL;
  int32_t *values = NULL;
  int32_t *vectors = NULL;
  ifx_segment *segment = NULL;
  ifx_fixture_writer writer;
  int writer_open = 0;
  void *firmware = ifx_firmware_open(adapter);
  if (firmware == NULL) {
    return -1;
  }

  unsigned max_vectors = 1u + knobs * 64u + 32u;
  values = malloc((knobs + effect->setup_words) * sizeof *values);
  vectors = malloc(max_vectors * knobs * sizeof *vectors);
  record = malloc(4u * (knobs + effect->setup_words));
  segment = malloc(IFX_EFFECT_SEGMENTS(knobs) * sizeof *segment);
  if (values == NULL || vectors == NULL || record == NULL || segment == NULL) {
    goto fail;
  }
  if (ifx_fixture_writer_open(&writer, path) != 0) {
    goto fail;
  }
  writer_open = 1;

  for (unsigned knob = 0; knob < knobs; knob++) {
    ifx_fixture_knob entry;
    memset(&entry, 0, sizeof entry);
    snprintf(entry.name, sizeof entry.name, "%s", effect->knob[knob].name);
    entry.min = effect->knob[knob].min;
    entry.max = effect->knob[knob].max;
    entry.def = effect->knob[knob].def;
    unsigned char bytes[IFX_FIX_KNOBS_SIZE];
    ifx_fixture_put_knob(bytes, &entry);
    if (ifx_fixture_writer_add(&writer, IFX_FIX_KNOBS, sizeof bytes, bytes) != 0) {
      goto fail;
    }
  }

  unsigned count = setup_vectors(effect, vectors, max_vectors);
  if (count == 0u) {
    goto fail;
  }
  for (unsigned knob = 0; knob < knobs; knob++) {
    values[knob] = effect->knob[knob].def;
  }
  for (unsigned vector = 0; vector < count; vector++) {
    const int32_t *want = vectors + (size_t)vector * knobs;
    for (unsigned knob = 0; knob < knobs; knob++) {
      if (want[knob] != values[knob] &&
          ifx_firmware_set_knob(firmware, knob, (int)want[knob]) != 0) {
        goto fail;
      }
      values[knob] = want[knob];
    }
    ifx_fixture_put_i32(record, knobs, want);
    if (ifx_firmware_words(firmware, values + knobs) != 0) {
      goto fail;
    }
    ifx_fixture_put_i32(record + 4u * knobs, effect->setup_words, values + knobs);
    if (ifx_fixture_writer_add(&writer, IFX_FIX_SETUP, 4u * (knobs + effect->setup_words),
                               record) != 0) {
      goto fail;
    }
    setup_count++;
  }

  for (unsigned preset = PRESET_FIRST; preset <= PRESET_LAST; preset++) {
    uint8_t bytes[IFX_EMU_PARAMETER_COUNT];
    int32_t preset_knobs[64];
    if (preset_values(effect, adapter->config, ifx_firmware_emu(firmware), preset, preset_knobs,
                      bytes) != 0) {
      continue;
    }
    if (ifx_firmware_load(firmware, bytes) != 0) {
      goto fail;
    }
    ifx_fixture_put_i32(record, knobs, preset_knobs);
    if (ifx_firmware_words(firmware, values + knobs) != 0) {
      goto fail;
    }
    ifx_fixture_put_i32(record + 4u * knobs, effect->setup_words, values + knobs);
    if (ifx_fixture_writer_add(&writer, IFX_FIX_SETUP, 4u * (knobs + effect->setup_words),
                               record) != 0) {
      goto fail;
    }
    setup_count++;
  }

  ifx_firmware_close(firmware);
  firmware = ifx_firmware_open(adapter);
  if (firmware == NULL) {
    goto fail;
  }
  for (unsigned knob = 0; knob < knobs; knob++) {
    values[knob] = effect->knob[knob].def;
  }
  {
    const unsigned cap = IFX_EFFECT_SEGMENTS(knobs);
    segments = ifx_effect_segments(effect, segment, cap);
    if (segments == 0u || segments > cap) {
      goto fail;
    }
  }
  unsigned long total = 0;
  for (unsigned i = 0; i < segments; i++) {
    total += segment[i].frames;
  }
  int32_t *input = malloc((total + 32u) * sizeof *input);
  int32_t *device_left = malloc((total + 32u) * sizeof *device_left);
  int32_t *device_right = malloc((total + 32u) * sizeof *device_right);
  if (input == NULL || device_left == NULL || device_right == NULL) {
    free(input);
    free(device_left);
    free(device_right);
    goto fail;
  }
  ifx_signal signal;
  ifx_signal_init(&signal);
  ifx_signal_fill(&signal, input, total + 32u);

  unsigned at = 0;
  int32_t current[64];
  for (unsigned knob = 0; knob < knobs; knob++) {
    current[knob] = effect->knob[knob].def;
    values[knob] = effect->knob[knob].def;
  }
  for (unsigned index = 0; index < segments; index++) {
    ifx_effect_values(effect, &segment[index], values);
    for (unsigned knob = 0; knob < knobs; knob++) {
      if (values[knob] != current[knob] &&
          ifx_firmware_set_knob(firmware, knob, (int)values[knob]) != 0) {
        goto audio_fail;
      }
    }
    memcpy(current, values, knobs * sizeof *current);
    ifx_fixture_put_segment(record, knobs, values, segment[index].frames);
    if (ifx_fixture_writer_add(&writer, IFX_FIX_SEGMENT, 4u * (knobs + 1u), record) != 0) {
      goto audio_fail;
    }
    for (uint32_t frame = 0; frame < segment[index].frames + (index + 1u == segments ? 32u : 0u);
         frame++) {
      int32_t left = 0;
      int32_t right = 0;
      if (ifx_firmware_frame(firmware, input[at], &left, &right) != 0) {
        goto audio_fail;
      }
      device_left[at] = left;
      device_right[at] = right;
      at++;
    }
  }
  for (unsigned long m = 0; m < total; m++) {
    ifx_fixture_put_output(record, device_left[m + 32u], device_right[m + 32u]);
    if (ifx_fixture_writer_add(&writer, IFX_FIX_OUTPUT, IFX_FIX_OUTPUT_SIZE, record) != 0) {
      goto audio_fail;
    }
  }
  frames = total;
  free(input);
  free(device_left);
  free(device_right);

  if (ifx_fixture_writer_close(&writer) != 0) {
    fprintf(stderr, "%s: cannot write %s\n", effect->name, path);
    goto fail;
  }
  printf("%-15s recorded %u settings, %u segments, %lu frames to %s\n", effect->name, setup_count,
         segments, frames, path);
  ifx_firmware_close(firmware);
  free(values);
  free(vectors);
  free(record);
  free(segment);
  return 0;

audio_fail:
  free(input);
  free(device_left);
  free(device_right);
fail:
  if (writer_open != 0) {
    ifx_fixture_writer_abort(&writer);
  }
  free(values);
  free(vectors);
  free(record);
  free(segment);
  if (firmware != NULL) {
    ifx_firmware_close(firmware);
  }
  return -1;
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "--list") == 0) {
#define IFX_LIST(name) puts(#name);
    IFX_EFFECTS(IFX_LIST)
#undef IFX_LIST
    puts("tables");
    return 0;
  }
  if (argc != 3) {
    fprintf(stderr, "usage: %s <name> <path>\n       %s --list\n", argv[0], argv[0]);
    return 2;
  }
  if (strcmp(argv[1], "tables") == 0) {
    ifx_emu *emu = ifx_emu_create(NULL);
    if (emu == NULL) {
      return 1;
    }
    uint8_t values[IFX_EMU_PARAMETER_COUNT] = {0};
    if (!ifx_emu_load(emu, 2, values) || !ifx_emu_settle(emu, NULL)) {
      ifx_emu_destroy(emu);
      return 1;
    }
    ifx_fixture_writer writer;
    if (ifx_fixture_writer_open(&writer, argv[2]) != 0) {
      ifx_emu_destroy(emu);
      return 1;
    }
    int status = record_tables(&writer, emu);
    ifx_emu_destroy(emu);
    if (status != 0) {
      ifx_fixture_writer_abort(&writer);
      return 1;
    }
    if (ifx_fixture_writer_close(&writer) != 0) {
      return 1;
    }
    printf("%-15s recorded %u tables, %u words to %s\n", "tables", (unsigned)(TABLE_COUNT + 1u),
           IFX_LEVEL_TABLE_WORDS + IFX_PAN_TABLE_WORDS + IFX_HUSH_THRESHOLD_TABLE_WORDS +
               IFX_HUSH_SLOPE_TABLE_WORDS + IFX_HUSH_RELEASE_TABLE_WORDS +
               IFX_DUCKER_RELEASE_TABLE_WORDS + IFX_GATE_DECAY_TABLE_WORDS +
               IFX_GATE_HOLD_TABLE_WORDS + IFX_COARSE_PITCH_TABLE_WORDS +
               IFX_FINE_PITCH_TABLE_WORDS + IFX_EMU_PARAMETER_COUNT +
               IFX_REVERB_TYPE_COUNT * IFX_REVERB_TYPE_TABLE_WORDS,
           argv[2]);
    return 0;
  }
  const ifx_firmware *adapters[] = {
#define IFX_FIRMWARE_POINTER(name) &ifx_firmware_##name,
      IFX_EFFECTS(IFX_FIRMWARE_POINTER)
#undef IFX_FIRMWARE_POINTER
  };
  for (unsigned i = 0; i < sizeof adapters / sizeof adapters[0]; i++) {
    if (strcmp(argv[1], adapters[i]->effect->name) == 0) {
      return record_effect(adapters[i], argv[2]) == 0 ? 0 : 1;
    }
  }
  fprintf(stderr, "%s: nothing to record is called %s\n", argv[0], argv[1]);
  return 2;
}
