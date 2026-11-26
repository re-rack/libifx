/* The checker: replays the recorded firmware answers through the native library,
 * offline, and checks the host-facing pieces the firmware has no answer for.
 * Prints one PASS or FAIL line per item, at most five detail lines per item, and
 * exits non-zero when anything failed. */
#include "ifx.h"
#include "ifx_dsp_math.h"
#include "ifx_fixture.h"
#include "ifx_process.h"
#include "ifx_tables.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIXTURE_DIR "tests/fixtures"
#define DETAIL_MAX 5u
#define HOST_FRAMES 4096u

static unsigned item_failures;
static unsigned item_shown;

static void __attribute__((format(printf, 1, 2))) detail(const char *format, ...) {
  va_list args;
  if (item_shown >= DETAIL_MAX) {
    return;
  }
  item_shown++;
  fputs("     ", stdout);
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
  putchar('\n');
}

static void failed(void) { item_failures++; }

static void expect(int ok, const char *what) {
  if (!ok) {
    detail("%s", what);
    failed();
  }
}

static uint32_t next_random(uint32_t *state) {
  *state = *state * 1664525u + 1013904223u;
  return *state >> 8;
}

/* --- the tables fixture -------------------------------------------------- */

typedef struct {
  const char *name;
  const int32_t *words;
  const uint8_t *bytes;
  uint32_t count;
} native_table;

static const native_table k_tables[] = {
    {"ifx_level_table", ifx_level_table, NULL, IFX_LEVEL_TABLE_WORDS},
    {"ifx_pan_table", ifx_pan_table, NULL, IFX_PAN_TABLE_WORDS},
    {"ifx_hush_threshold_table", ifx_hush_threshold_table, NULL, IFX_HUSH_THRESHOLD_TABLE_WORDS},
    {"ifx_hush_slope_table", ifx_hush_slope_table, NULL, IFX_HUSH_SLOPE_TABLE_WORDS},
    {"ifx_hush_release_table", ifx_hush_release_table, NULL, IFX_HUSH_RELEASE_TABLE_WORDS},
    {"ifx_ducker_release_table", ifx_ducker_release_table, NULL, IFX_DUCKER_RELEASE_TABLE_WORDS},
    {"ifx_gate_decay_table", ifx_gate_decay_table, NULL, IFX_GATE_DECAY_TABLE_WORDS},
    {"ifx_gate_hold_table", ifx_gate_hold_table, NULL, IFX_GATE_HOLD_TABLE_WORDS},
    {"ifx_coarse_pitch_table", ifx_coarse_pitch_table, NULL, IFX_COARSE_PITCH_TABLE_WORDS},
    {"ifx_fine_pitch_table", ifx_fine_pitch_table, NULL, IFX_FINE_PITCH_TABLE_WORDS},
    {"ifx_window_table", NULL, ifx_window_table, IFX_WINDOW_TABLE_WORDS},
};

#define TABLE_COUNT (sizeof k_tables / sizeof k_tables[0])

static int32_t native_word(const native_table *table, uint32_t index) {
  if (table->words != NULL) {
    return table->words[index];
  }
  return table->bytes != NULL ? table->bytes[index] : 0;
}

static const native_table *find_table(const char *name) {
  for (size_t i = 0; i < TABLE_COUNT; i++) {
    if (strcmp(name, k_tables[i].name) == 0) {
      return &k_tables[i];
    }
  }
  return NULL;
}

/* Every table ifx_tables.c keeps, word for word against the firmware's. */
static unsigned check_tables(void) {
  item_failures = 0;
  item_shown = 0;
  ifx_fixture fixture;
  if (ifx_fixture_load(&fixture, FIXTURE_DIR "/tables.bin") != 0) {
    printf("%-17s FAIL  cannot load " FIXTURE_DIR "/tables.bin\n", "tables");
    return 1;
  }
  uint32_t index_count = 0;
  uint32_t index_size = 0;
  uint32_t word_count = 0;
  uint32_t word_size = 0;
  const void *index_records =
      ifx_fixture_records(&fixture, IFX_FIX_TABLE_INDEX, &index_count, &index_size);
  const void *words = ifx_fixture_records(&fixture, IFX_FIX_TABLE_WORDS, &word_count, &word_size);
  unsigned matched = 0;
  if (index_records == NULL || words == NULL || index_size != IFX_FIX_TABLE_INDEX_SIZE ||
      word_size != IFX_FIX_TABLE_WORDS_SIZE) {
    detail("the fixture holds no usable table sections");
    failed();
    goto done;
  }
  unsigned char seen[TABLE_COUNT];
  memset(seen, 0, sizeof seen);
  for (uint32_t record = 0; record < index_count; record++) {
    ifx_fixture_table index;
    ifx_fixture_get_table((const unsigned char *)index_records + (size_t)index_size * record,
                          &index);
    const native_table *table = find_table(index.name);
    if (table == NULL) {
      detail("the fixture holds %s, which the library does not", index.name);
      failed();
      continue;
    }
    if (index.count != table->count) {
      detail("%s is %u words, the library has %u", index.name, (unsigned)index.count,
             (unsigned)table->count);
      failed();
      continue;
    }
    seen[table - k_tables] = 1;
    for (uint32_t word = 0; word < index.count; word++) {
      const int32_t want = ifx_fixture_get_word(
          (const unsigned char *)words + IFX_FIX_TABLE_WORDS_SIZE * (size_t)(index.first + word));
      const int32_t got = native_word(table, word);
      if (want != got) {
        detail("%s word %u: %06X, firmware %06X", index.name, (unsigned)word,
               (uint32_t)got & 0xFFFFFFu, (uint32_t)want & 0xFFFFFFu);
        failed();
        break;
      }
    }
  }
  unsigned missing = 0;
  for (size_t i = 0; i < TABLE_COUNT; i++) {
    if (seen[i] != 0u) {
      matched++;
    } else if (missing++ == 0u) {
      detail("the fixture holds no %s", k_tables[i].name);
      failed();
    }
  }
  if (missing != 0u) {
    detail("the fixture covers %u of the %u tables", matched, (unsigned)TABLE_COUNT);
    failed();
  }

done:
  ifx_fixture_free(&fixture);
  int ok = item_failures == 0;
  printf("%-17s %s  %u tables, %u words\n", "tables", ok ? "PASS" : "FAIL", index_count,
         word_count);
  return ok ? 0 : 1;
}

/* --- the process entry points ------------------------------------------- */

typedef struct {
  int32_t bias;
  int32_t scale;
} ifx_host_config;

typedef struct {
  ifx_process_block block;
  int32_t blocks;
} ifx_host_state;

static void host_state_init(ifx_host_state *state, const ifx_host_config *config) {
  (void)config;
  ifx_process_block_init(&state->block);
  state->blocks = 0;
}

/* Wrap an integer to a valid Q23 word. */
static int32_t host_word(int64_t value) { return ifx_s24((uint32_t)(int32_t)value); }

/* Each block writes a running sum of its input, so a reordered frame shows, and
 * adds the block counter and the config of the frame that completed it. */
static void host_block(ifx_host_state *state, const ifx_host_config *config,
                       const int32_t in[IFX_BLOCK_FRAMES], int32_t out_l[IFX_BLOCK_FRAMES],
                       int32_t out_r[IFX_BLOCK_FRAMES]) {
  int64_t run = 0;
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    run += in[i];
    out_l[i] = host_word(run + config->bias + state->blocks);
    out_r[i] = host_word(-run + config->scale * (int64_t)(i + 1u) + state->blocks);
  }
  state->blocks++;
}

void ifx_host_process_split(ifx_host_state *state, const ifx_host_config *config, const int32_t *in,
                            int32_t *out_l, int32_t *out_r, size_t frames);
void ifx_host_process_f32(ifx_host_state *state, const ifx_host_config *config, const float *in,
                          float *out_l, float *out_r, size_t frames);
void ifx_host_process_interleaved(ifx_host_state *state, const ifx_host_config *config,
                                  const int32_t *in, int32_t *out, size_t frames);
void ifx_host_process_frame(ifx_host_state *state, const ifx_host_config *config, int32_t in,
                            int32_t out[2]);

IFX_PROCESS_DEFINE(host, host_block)

typedef enum {
  HOST_SPLIT,
  HOST_FLOAT,
  HOST_INTERLEAVED,
  HOST_FRAME,
} host_mode;

static size_t next_chunk(uint32_t *seed, size_t remaining, unsigned *index) {
  static const size_t k_sizes[] = {0u, 1u, 31u, 32u, 33u, 7u, 64u, 255u, 512u, 1024u, 3u, 0u, 1u};
  size_t count;
  if (*index < sizeof k_sizes / sizeof k_sizes[0]) {
    count = k_sizes[(*index)++];
  } else {
    count = 1u + next_random(seed) % 200u;
  }
  return count > remaining ? remaining : count;
}

/* One pass through one entry point with the same chunk plan for every mode. */
static int host_render(host_mode mode, const int32_t *in, size_t frames,
                       const ifx_host_config *config, int32_t *left, int32_t *right,
                       uint32_t seed) {
  ifx_host_state state;
  host_state_init(&state, config);
  if (mode == HOST_FLOAT) {
    float *fin = malloc(frames * sizeof *fin);
    float *fl = malloc(frames * sizeof *fl);
    float *fr = malloc(frames * sizeof *fr);
    if (fin == NULL || fl == NULL || fr == NULL) {
      free(fin);
      free(fl);
      free(fr);
      return -1;
    }
    for (size_t i = 0; i < frames; i++) {
      fin[i] = (float)in[i] * 0x1p-23f;
    }
    size_t pos = 0;
    unsigned index = 0;
    while (pos < frames) {
      size_t count = next_chunk(&seed, frames - pos, &index);
      ifx_host_process_f32(&state, config, fin + pos, fl + pos, fr + pos, count);
      pos += count;
    }
    ifx_f32_to_q23(left, fl, frames);
    ifx_f32_to_q23(right, fr, frames);
    free(fin);
    free(fl);
    free(fr);
    return 0;
  }
  if (mode == HOST_INTERLEAVED) {
    int32_t *pairs = malloc(2u * frames * sizeof *pairs);
    if (pairs == NULL) {
      return -1;
    }
    size_t pos = 0;
    unsigned index = 0;
    while (pos < frames) {
      size_t count = next_chunk(&seed, frames - pos, &index);
      ifx_host_process_interleaved(&state, config, in + pos, pairs + 2u * pos, count);
      pos += count;
    }
    for (size_t i = 0; i < frames; i++) {
      left[i] = pairs[2u * i];
      right[i] = pairs[2u * i + 1u];
    }
    free(pairs);
    return 0;
  }
  if (mode == HOST_FRAME) {
    for (size_t i = 0; i < frames; i++) {
      int32_t out[2];
      ifx_host_process_frame(&state, config, in[i], out);
      left[i] = out[0];
      right[i] = out[1];
    }
    return 0;
  }
  size_t pos = 0;
  unsigned index = 0;
  while (pos < frames) {
    size_t count = next_chunk(&seed, frames - pos, &index);
    ifx_host_process_split(&state, config, in + pos, left + pos, right + pos, count);
    pos += count;
  }
  return 0;
}

static unsigned check_context(void) {
  item_failures = 0;
  item_shown = 0;
  ifx_context context;
  context.rate = IFX_DEVICE_RATE;
  expect(ifx_context_init(NULL, IFX_DEVICE_RATE) == -1, "a NULL context is accepted");
  expect(ifx_context_init(&context, 0.0) == -1 && ifx_context_rate(&context) == 0.0,
         "a rate of zero is accepted");
  expect(ifx_context_init(&context, -1.0) == -1 && ifx_context_rate(&context) == 0.0,
         "a negative rate is accepted");
  expect(ifx_context_init(&context, (double)NAN) == -1 && ifx_context_rate(&context) == 0.0,
         "a NaN rate is accepted");
  expect(ifx_context_init(&context, (double)INFINITY) == -1 && ifx_context_rate(&context) == 0.0,
         "an infinite rate is accepted");
  expect(ifx_context_init(&context, IFX_DEVICE_RATE) == 0 &&
             ifx_context_rate(&context) == IFX_DEVICE_RATE,
         "the device rate is refused");
  unsigned checks = 6;
  printf("%-17s %s  %u contexts\n", "host/context", item_failures == 0 ? "PASS" : "FAIL", checks);
  return item_failures == 0 ? 0 : 1;
}

/* Exactly 32 frames of zero rest output, then the block's answer; the config of
 * the frame that completed the block is the one it uses. */
static unsigned check_latency(void) {
  item_failures = 0;
  item_shown = 0;
  static int32_t in[96];
  static int32_t out_l[96];
  static int32_t out_r[96];
  const ifx_host_config a = {1000, 3};
  const ifx_host_config b = {-2000, 5};
  ifx_host_state state;
  host_state_init(&state, &a);
  for (unsigned i = 0; i < 96u; i++) {
    in[i] = (int32_t)((i * 7919u) % 1000003u) - 500000;
    const ifx_host_config *config = i == 31u ? &b : &a;
    int32_t out[2];
    ifx_host_process_frame(&state, config, in[i], out);
    out_l[i] = out[0];
    out_r[i] = out[1];
  }
  int ok = 1;
  for (unsigned i = 0; i < 32u; i++) {
    if (out_l[i] != 0 || out_r[i] != 0) {
      detail("output frame %u is not the rest output", i);
      failed();
      ok = 0;
      break;
    }
  }
  for (unsigned block = 0; block < 2u && ok != 0; block++) {
    const ifx_host_config *config = block == 0u ? &b : &a;
    int64_t run = 0;
    for (unsigned i = 0; i < 32u; i++) {
      run += in[32u * block + i];
      const int32_t want_l = host_word(run + config->bias + (int32_t)block);
      const int32_t want_r = host_word(-run + config->scale * (int64_t)(i + 1u) + (int32_t)block);
      const unsigned at = 32u * (block + 1u) + i;
      if (out_l[at] != want_l || out_r[at] != want_r) {
        detail("output frame %u is not block %u's answer", at, block);
        failed();
        ok = 0;
        break;
      }
    }
  }
  printf("%-17s %s  %u frames, config at the completing frame\n", "host/latency",
         item_failures == 0 ? "PASS" : "FAIL", 96u);
  return item_failures == 0 ? 0 : 1;
}

static unsigned check_entry_points(void) {
  item_failures = 0;
  item_shown = 0;
  int32_t *in = malloc(HOST_FRAMES * sizeof *in);
  int32_t *want_l = malloc(HOST_FRAMES * sizeof *want_l);
  int32_t *want_r = malloc(HOST_FRAMES * sizeof *want_r);
  int32_t *got_l = malloc(HOST_FRAMES * sizeof *got_l);
  int32_t *got_r = malloc(HOST_FRAMES * sizeof *got_r);
  if (in == NULL || want_l == NULL || want_r == NULL || got_l == NULL || got_r == NULL) {
    detail("out of memory");
    failed();
    goto done;
  }
  const ifx_host_config config = {12345, 7};
  uint32_t state = 0x12345678u;
  for (size_t i = 0; i < HOST_FRAMES; i++) {
    state = state * 1664525u + 1013904223u;
    in[i] = (int32_t)(state >> 8) - 0x800000;
  }
  in[0] = 0x7FFFFF;
  in[1] = -0x800000;
  in[2] = 0;
  if (host_render(HOST_SPLIT, in, HOST_FRAMES, &config, want_l, want_r, 0x9E3779B9u) != 0) {
    detail("out of memory");
    failed();
    goto done;
  }
  const host_mode modes[] = {HOST_FLOAT, HOST_INTERLEAVED, HOST_FRAME};
  const char *const names[] = {"float", "interleaved", "frame"};
  for (size_t mode = 0; mode < sizeof modes / sizeof modes[0]; mode++) {
    if (host_render(modes[mode], in, HOST_FRAMES, &config, got_l, got_r, 0x9E3779B9u) != 0) {
      detail("%s: out of memory", names[mode]);
      failed();
      continue;
    }
    for (size_t i = 0; i < HOST_FRAMES; i++) {
      if (got_l[i] != want_l[i] || got_r[i] != want_r[i]) {
        detail("%s differs at frame %zu: L %d/%d R %d/%d", names[mode], i, (int)got_l[i],
               (int)want_l[i], (int)got_r[i], (int)want_r[i]);
        failed();
        break;
      }
    }
  }
done:
  free(in);
  free(want_l);
  free(want_r);
  free(got_l);
  free(got_r);
  printf("%-17s %s  %u frames through four entry points\n", "host/entries",
         item_failures == 0 ? "PASS" : "FAIL", HOST_FRAMES);
  return item_failures == 0 ? 0 : 1;
}

static unsigned check_in_place(void) {
  item_failures = 0;
  item_shown = 0;
  int32_t *in = malloc(HOST_FRAMES * sizeof *in);
  int32_t *want_l = malloc(HOST_FRAMES * sizeof *want_l);
  int32_t *want_r = malloc(HOST_FRAMES * sizeof *want_r);
  int32_t *got_l = malloc(HOST_FRAMES * sizeof *got_l);
  int32_t *got_r = malloc(HOST_FRAMES * sizeof *got_r);
  float *fin = malloc(HOST_FRAMES * sizeof *fin);
  float *fout = malloc(HOST_FRAMES * sizeof *fout);
  if (in == NULL || want_l == NULL || want_r == NULL || got_l == NULL || got_r == NULL ||
      fin == NULL || fout == NULL) {
    detail("out of memory");
    failed();
    goto done;
  }
  const ifx_host_config config = {-321, 4};
  for (size_t i = 0; i < HOST_FRAMES; i++) {
    in[i] = (int32_t)((i * 15485863u) % 16777216u) - 0x800000;
    fin[i] = (float)in[i] * 0x1p-23f;
  }
  if (host_render(HOST_SPLIT, in, HOST_FRAMES, &config, want_l, want_r, 0x5A5A5A5Au) != 0) {
    detail("out of memory");
    failed();
    goto done;
  }

  int32_t *copy = malloc(HOST_FRAMES * sizeof *copy);
  if (copy == NULL) {
    detail("out of memory");
    failed();
    goto done;
  }
  memcpy(copy, in, HOST_FRAMES * sizeof *in);
  ifx_host_state state;
  host_state_init(&state, &config);
  ifx_host_process_split(&state, &config, copy, copy, got_r, HOST_FRAMES);
  if (memcmp(copy, want_l, HOST_FRAMES * sizeof *want_l) != 0 ||
      memcmp(got_r, want_r, HOST_FRAMES * sizeof *want_r) != 0) {
    detail("split with the left output in place differs");
    failed();
  }
  memcpy(copy, in, HOST_FRAMES * sizeof *in);
  host_state_init(&state, &config);
  ifx_host_process_split(&state, &config, copy, got_l, copy, HOST_FRAMES);
  if (memcmp(got_l, want_l, HOST_FRAMES * sizeof *want_l) != 0 ||
      memcmp(copy, want_r, HOST_FRAMES * sizeof *want_r) != 0) {
    detail("split with the right output in place differs");
    failed();
  }

  for (size_t i = 0; i < HOST_FRAMES; i++) {
    fin[i] = (float)in[i] * 0x1p-23f;
  }
  host_state_init(&state, &config);
  ifx_host_process_f32(&state, &config, fin, fin, fout, HOST_FRAMES);
  ifx_f32_to_q23(got_l, fin, HOST_FRAMES);
  ifx_f32_to_q23(got_r, fout, HOST_FRAMES);
  if (memcmp(got_l, want_l, HOST_FRAMES * sizeof *want_l) != 0 ||
      memcmp(got_r, want_r, HOST_FRAMES * sizeof *want_r) != 0) {
    detail("float with the left output in place differs");
    failed();
  }
  for (size_t i = 0; i < HOST_FRAMES; i++) {
    fin[i] = (float)in[i] * 0x1p-23f;
  }
  host_state_init(&state, &config);
  ifx_host_process_f32(&state, &config, fin, fout, fin, HOST_FRAMES);
  ifx_f32_to_q23(got_l, fout, HOST_FRAMES);
  ifx_f32_to_q23(got_r, fin, HOST_FRAMES);
  if (memcmp(got_l, want_l, HOST_FRAMES * sizeof *want_l) != 0 ||
      memcmp(got_r, want_r, HOST_FRAMES * sizeof *want_r) != 0) {
    detail("float with the right output in place differs");
    failed();
  }
  free(copy);
done:
  free(in);
  free(want_l);
  free(want_r);
  free(got_l);
  free(got_r);
  free(fin);
  free(fout);
  printf("%-17s %s  split and float in place\n", "host/in-place",
         item_failures == 0 ? "PASS" : "FAIL");
  return item_failures == 0 ? 0 : 1;
}

static unsigned check_junk(void) {
  item_failures = 0;
  item_shown = 0;
  int32_t *clean = malloc(HOST_FRAMES * sizeof *clean);
  int32_t *dirty = malloc(HOST_FRAMES * sizeof *dirty);
  int32_t *want_l = malloc(HOST_FRAMES * sizeof *want_l);
  int32_t *want_r = malloc(HOST_FRAMES * sizeof *want_r);
  int32_t *got_l = malloc(HOST_FRAMES * sizeof *got_l);
  int32_t *got_r = malloc(HOST_FRAMES * sizeof *got_r);
  if (clean == NULL || dirty == NULL || want_l == NULL || want_r == NULL || got_l == NULL ||
      got_r == NULL) {
    detail("out of memory");
    failed();
    goto done;
  }
  const ifx_host_config config = {7, 2};
  for (size_t i = 0; i < HOST_FRAMES; i++) {
    clean[i] = (int32_t)(((i * 2654435761u) >> 8) & 0xFFFFFFu) - 0x800000;
    dirty[i] = (int32_t)((uint32_t)clean[i] | 0x5A000000u);
  }
  if (host_render(HOST_SPLIT, clean, HOST_FRAMES, &config, want_l, want_r, 0x1234u) != 0 ||
      host_render(HOST_SPLIT, dirty, HOST_FRAMES, &config, got_l, got_r, 0x1234u) != 0) {
    detail("out of memory");
    failed();
    goto done;
  }
  if (memcmp(got_l, want_l, HOST_FRAMES * sizeof *want_l) != 0 ||
      memcmp(got_r, want_r, HOST_FRAMES * sizeof *want_r) != 0) {
    detail("split reads bits above 23");
    failed();
  }
  if (host_render(HOST_INTERLEAVED, dirty, HOST_FRAMES, &config, got_l, got_r, 0x1234u) != 0) {
    detail("out of memory");
    failed();
    goto done;
  }
  if (memcmp(got_l, want_l, HOST_FRAMES * sizeof *want_l) != 0 ||
      memcmp(got_r, want_r, HOST_FRAMES * sizeof *want_r) != 0) {
    detail("interleaved reads bits above 23");
    failed();
  }
  if (host_render(HOST_FRAME, dirty, HOST_FRAMES, &config, got_l, got_r, 0x1234u) != 0) {
    detail("out of memory");
    failed();
    goto done;
  }
  if (memcmp(got_l, want_l, HOST_FRAMES * sizeof *want_l) != 0 ||
      memcmp(got_r, want_r, HOST_FRAMES * sizeof *want_r) != 0) {
    detail("frame reads bits above 23");
    failed();
  }
done:
  free(clean);
  free(dirty);
  free(want_l);
  free(want_r);
  free(got_l);
  free(got_r);
  printf("%-17s %s  upper input bits ignored\n", "host/junk", item_failures == 0 ? "PASS" : "FAIL");
  return item_failures == 0 ? 0 : 1;
}

static unsigned check_host(void) {
  unsigned failures = 0;
  failures += check_context();
  failures += check_latency();
  failures += check_entry_points();
  failures += check_in_place();
  failures += check_junk();
  return failures;
}

static void usage(const char *program) {
  fprintf(stderr, "usage: %s [tables] [host]\n", program);
  fprintf(stderr, "  no name: all of them\n");
}

int main(int argc, char **argv) {
  unsigned failures = 0;
  int tables = argc == 1;
  int host = argc == 1;
  for (int arg = 1; arg < argc; arg++) {
    if (strcmp(argv[arg], "tables") == 0) {
      tables = 1;
    } else if (strcmp(argv[arg], "host") == 0) {
      host = 1;
    } else {
      usage(argv[0]);
      return 2;
    }
  }
  if (tables != 0) {
    failures += check_tables();
  }
  if (host != 0) {
    failures += check_host();
  }
  return failures == 0 ? 0 : 1;
}
