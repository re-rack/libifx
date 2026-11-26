/* Fixture recorder CLI: ifx_record <name> <path>, where <name> is "tables", or
 * --list. The firmware harness is linked in; nothing else here knows about it. */
#include "ifx_dsp_board.h"
#include "ifx_dsp_math.h"
#include "ifx_emu.h"
#include "ifx_fixture.h"
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
};

#define TABLE_COUNT (sizeof k_tables / sizeof k_tables[0])
#define WINDOW_EPROM_OFFSET 0x12C8u

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
  int32_t *words = malloc(IFX_COARSE_PITCH_TABLE_WORDS * sizeof *words);
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

  const char *path = ifx_emu_eprom_path(emu);
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "tables: cannot open %s\n", path);
    return -1;
  }
  if (fseek(file, (long)WINDOW_EPROM_OFFSET, SEEK_SET) != 0) {
    fclose(file);
    return -1;
  }
  uint8_t bytes[IFX_WINDOW_TABLE_WORDS];
  int ok = fread(bytes, 1, sizeof bytes, file) == sizeof bytes;
  fclose(file);
  if (!ok) {
    fprintf(stderr, "tables: %s ends before the parameter window table\n", path);
    return -1;
  }
  int32_t window[IFX_WINDOW_TABLE_WORDS];
  for (unsigned i = 0; i < IFX_WINDOW_TABLE_WORDS; i++) {
    window[i] = bytes[i];
  }
  return add_table(writer, "ifx_window_table", &first, window, IFX_WINDOW_TABLE_WORDS);
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "--list") == 0) {
    puts("tables");
    return 0;
  }
  if (argc != 3) {
    fprintf(stderr, "usage: %s <name> <path>\n       %s --list\n", argv[0], argv[0]);
    return 2;
  }
  if (strcmp(argv[1], "tables") != 0) {
    fprintf(stderr, "%s: nothing to record is called %s\n", argv[0], argv[1]);
    return 2;
  }
  ifx_emu *emu = ifx_emu_create(NULL);
  if (emu == NULL) {
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
    fprintf(stderr, "tables: cannot write %s\n", argv[2]);
    return 1;
  }
  printf("%-15s recorded %u tables, %u words to %s\n", "tables", (unsigned)(TABLE_COUNT + 1u),
         IFX_LEVEL_TABLE_WORDS + IFX_PAN_TABLE_WORDS + IFX_HUSH_THRESHOLD_TABLE_WORDS +
             IFX_HUSH_SLOPE_TABLE_WORDS + IFX_HUSH_RELEASE_TABLE_WORDS +
             IFX_DUCKER_RELEASE_TABLE_WORDS + IFX_GATE_DECAY_TABLE_WORDS +
             IFX_GATE_HOLD_TABLE_WORDS + IFX_COARSE_PITCH_TABLE_WORDS + IFX_FINE_PITCH_TABLE_WORDS +
             IFX_WINDOW_TABLE_WORDS,
         argv[2]);
  return 0;
}
