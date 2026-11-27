/* Bring-up of the Intellifex LTD through ifx_emu: the panel is driven the way a
 * hand drives it, the converter is fed tones and impulses, and the DSP's words
 * come back out the other side.
 *
 * Usage: ifx_mcu_bringup [command ...]
 *   500          run 500 ms of emulated time
 *   show         print the panel, the RAM state and the traffic counters
 *   button:x     press and release button x (recall, store, compare, config,
 *                bypass)
 *   press:x      press and hold x
 *   release:x    release x
 *   turn:x:n     turn knob x (preset, function, parameter, adjust) n detents
 *   recall:n     recall preset n (1..160)
 *   set:n=v      bring parameter n onto the panel and set it to v
 *   silence:ms   feed silence and print what came out
 *   tone:hz:ms   feed a -12 dBFS sine and print what came out
 *   impulse:ms   feed one full-scale sample and print the peaks that follow
 *   messages     print the parameter messages the DSP has drained
 *   factory      rebuild the battery the way the firmware's restore does and reboot
 *   erase        erase the battery and reboot
 *   dump         print the display buffer and every parameter
 *   save:path    write the battery out
 *   load:path    read the battery back and reboot
 *   send:i=v     send one parameter through the MCU's C:0EE0
 *   align        feed silence to the next block start
 *   phase        print the input block phase and pass count
 *   load:c:i=v   make a user preset for configuration c (others at the factory
 *                values), recall it, and leave it on the panel
 *   settle       feed silence until the configuration is at rest
 *   verify:audio run the pour-in check on each factory configuration
 *   verify:timing check latency, message timing and load/settle on all six
 *   verify:factory dial the panel's FACTORY RESTORE and compare the battery it
 *                leaves behind with the modelled one
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsp_cpu.h"
#include "ifx_dsp_board.h"
#include "ifx_emu.h"
#include "ifx_mcu_board.h"

/* 33 MHz of DSP clock, one frame per sample period: the converters run at the
 * frame rate, not at a round number. */
#define IFX_DSP_CLOCK_HZ 33000000u
#define IFX_CLOCKS_PER_FRAME IFX_DSP_BOARD_FRAME_CYCLES
#define IFX_SAMPLE_RATE_HZ ((double)IFX_DSP_CLOCK_HZ / (double)IFX_CLOCKS_PER_FRAME)
#define IFX_Q23_FULL_SCALE 8388608.0
/* -12 dBFS, in the 16 bits the A/D converter actually resolves. */
#define IFX_TONE_AMPLITUDE 2107136
#define IFX_IMPULSE_AMPLITUDE 0x400000 /* -6 dBFS: an echo still fits */
/* -78 dBFS: below the 16-bit converter's own noise, far above a silent mixer. */
#define IFX_SILENCE_LIMIT 1024

static ifx_emu *g_emu;

/* Integer arithmetic: the counts line up with the frame the emulator runs. */
static size_t frames_for_ms(unsigned ms) {
  return ((size_t)ms * IFX_DSP_CLOCK_HZ + IFX_CLOCKS_PER_FRAME * 500u) /
         (IFX_CLOCKS_PER_FRAME * 1000u);
}

static void run_ms(unsigned ms) { ifx_emu_run_ms(g_emu, ms); }

static void press(ifx_mcu_board_button button) {
  ifx_emu_button(g_emu, button, true);
  run_ms(2);
}

static void release(ifx_mcu_board_button button) {
  ifx_emu_button(g_emu, button, false);
  run_ms(2);
}

typedef struct {
  int32_t min;
  int32_t max;
  int32_t mean;
  int32_t peak;
  double rms;
  uint64_t checksum;
} channel_stats;

/* The converter's silence is not zero: the mixer adds its own offset, so the
 * measures that matter are the mean and what swings around it. */
static channel_stats measure(const int32_t *samples, size_t frames) {
  channel_stats stats = {INT32_MAX, INT32_MIN, 0, 0, 0.0, 0xCBF29CE484222325u};
  int64_t sum = 0;
  for (size_t i = 0; i < frames; ++i) {
    int32_t value = samples[i];
    sum += value;
    stats.min = value < stats.min ? value : stats.min;
    stats.max = value > stats.max ? value : stats.max;
  }
  stats.mean = (int32_t)(sum / (int64_t)frames);
  double sum_squares = 0.0;
  for (size_t i = 0; i < frames; ++i) {
    int64_t deviation = (int64_t)samples[i] - stats.mean;
    deviation = deviation < 0 ? -deviation : deviation;
    stats.peak = deviation > stats.peak ? (int32_t)deviation : stats.peak;
    double value = (double)((int64_t)samples[i] - stats.mean);
    sum_squares += value * value;
    stats.checksum ^= (uint32_t)samples[i];
    stats.checksum *= 0x100000001B3u;
  }
  stats.rms = sqrt(sum_squares / (double)frames);
  return stats;
}

static void print_channel(const char *name, const channel_stats *stats) {
  printf("  %s min=%d max=%d mean=%d peak=%d rms=%.2f dBFS fnv=%016llx\n", name, (int)stats->min,
         (int)stats->max, (int)stats->mean, (int)stats->peak,
         20.0 * log10(stats->rms / IFX_Q23_FULL_SCALE), (unsigned long long)stats->checksum);
}

/* One sine period's worth of samples, reused as is: every chunk then starts on
 * the same phase, so two runs of the same commands compare frame for frame. */
static int32_t *tone_samples(double hz, unsigned ms) {
  size_t frames = frames_for_ms(ms);
  int32_t *samples = calloc(frames != 0u ? frames : 1u, sizeof(*samples));
  if (samples == NULL) {
    return NULL;
  }
  for (size_t i = 0; i < frames; ++i) {
    double phase = 2.0 * 3.14159265358979323846 * hz * (double)i / IFX_SAMPLE_RATE_HZ;
    samples[i] = (int32_t)(IFX_TONE_AMPLITUDE * sin(phase));
  }
  return samples;
}

static bool feed(ifx_emu *emu, const int32_t *samples, size_t frames, bool adc16, int32_t *left,
                 int32_t *right) {
  ifx_emu_feed request = {samples, frames, adc16};
  return ifx_emu_audio(emu, &request, left, right);
}

/* Groups the samples that stand above an eighth of the peak and reports each
 * group's own peak: an impulse train comes back as one group per echo. */
static size_t collect_peaks(const int32_t *samples, size_t frames, int32_t mean, size_t *positions,
                            int32_t *values, size_t capacity) {
  int64_t peak = 0;
  for (size_t i = 0; i < frames; ++i) {
    int64_t value = (int64_t)samples[i] - mean;
    value = value < 0 ? -value : value;
    peak = value > peak ? value : peak;
  }
  if (peak == 0) {
    return 0;
  }
  int64_t threshold = peak / 8;
  bool grouped = false;
  size_t count = 0;
  size_t start = 0;
  size_t gap = 0;
  int64_t group_peak = 0;
  int64_t group_magnitude = 0;
  for (size_t i = 0; i <= frames; ++i) {
    int64_t sample = i < frames ? (int64_t)samples[i] - mean : 0;
    int64_t value = sample < 0 ? -sample : sample;
    if (value > threshold) {
      if (!grouped) {
        grouped = true;
        start = i;
        group_peak = sample;
        group_magnitude = value;
      }
      if (value > group_magnitude) {
        group_peak = sample;
        group_magnitude = value;
      }
      gap = 0;
      continue;
    }
    if (!grouped || ++gap <= 4u) {
      continue;
    }
    if (count < capacity) {
      positions[count] = start;
      values[count] = (int32_t)group_peak;
    }
    ++count;
    grouped = false;
  }
  return count;
}

static void print_peaks(const int32_t *samples, size_t frames, int32_t mean) {
  size_t positions[12];
  int32_t values[12];
  size_t count = collect_peaks(samples, frames, mean, positions, values, 12u);
  if (count == 0) {
    printf("  peaks: none\n");
    return;
  }
  printf("  peaks (%zu):", count);
  for (size_t i = 0; i < count && i < 12u; ++i) {
    printf(" %zu=%d", positions[i], (int)values[i]);
  }
  printf("\n");
}

static void show(void) {
  char text[IFX_MCU_BOARD_DISPLAY_WIDTH + 1u];
  const ifx_mcu_board *mcu = ifx_emu_mcu(g_emu);
  const ifx_dsp_board *dsp = ifx_emu_dsp(g_emu);
  unsigned writes = 0;
  unsigned reads = 0;
  ifx_emu_display(g_emu, text);
  ifx_emu_host_traffic(g_emu, &writes, &reads);
  printf("[%llu us] display \"%s\" pc=%04x leds=%02x seg=%02x%02x digit=%02x%02x\n",
         (unsigned long long)(ifx_emu_clocks(g_emu) / IFX_MCU_BOARD_CLOCKS_PER_CYCLE), text,
         (unsigned)ifx_mcu_board_pc(mcu), (unsigned)ifx_mcu_board_leds(mcu),
         (unsigned)ifx_mcu_board_xdata(mcu, 0x8005u), (unsigned)ifx_mcu_board_xdata(mcu, 0x8007u),
         (unsigned)ifx_mcu_board_xdata(mcu, 0x8006u), (unsigned)ifx_mcu_board_xdata(mcu, 0x8004u));
  printf("  preset=%u config=%u page=%02x param=%02x value=%u mode=%02x\n", ifx_emu_preset(g_emu),
         ifx_emu_config(g_emu), ifx_emu_page(g_emu), ifx_emu_index(g_emu),
         ifx_emu_value(g_emu, ifx_emu_index(g_emu)), (unsigned)ifx_mcu_board_iram(mcu, 0x30u));
  printf("  host w=%u r=%u  dsp %s frames=%llu overruns=%u underruns=%u\n", writes, reads,
         ifx_dsp_board_running(dsp) ? "running" : "halted",
         (unsigned long long)ifx_emu_frames(g_emu), ifx_emu_overruns(g_emu),
         ifx_emu_underruns(g_emu));
}

static void dump(void) {
  char text[IFX_MCU_BOARD_DISPLAY_WIDTH + 1u];
  const ifx_mcu_board *mcu = ifx_emu_mcu(g_emu);
  ifx_emu_display(g_emu, text);
  printf("  display \"%s\"\n", text);
  printf("  buffer:");
  for (unsigned i = 0; i < IFX_MCU_BOARD_DISPLAY_WIDTH; ++i) {
    printf(" %02x", (unsigned)ifx_mcu_board_iram(mcu, (uint8_t)(0x70u + i)));
  }
  printf("\n");
  for (unsigned index = 0; index < IFX_EMU_PARAMETER_COUNT; index += 8u) {
    printf("  %02x:", index);
    for (unsigned i = 0; i < 8u && index + i < IFX_EMU_PARAMETER_COUNT; ++i) {
      printf(" %3u", ifx_emu_value(g_emu, index + i));
    }
    printf("\n");
  }
}

static void messages(void) {
  unsigned count = 0;
  const ifx_emu_message *log = ifx_emu_messages(g_emu, &count);
  printf("  %u parameter messages\n", count);
  for (unsigned i = 0; i < count; ++i) {
    printf("  frame %llu -> %llu words=%u\n", (unsigned long long)log[i].frame_in,
           (unsigned long long)log[i].frame_applied, log[i].words);
  }
}

static bool encoder_named(const char *name, ifx_mcu_board_encoder *encoder) {
  static const char *const names[IFX_MCU_BOARD_ENCODER_COUNT] = {"preset", "function", "parameter",
                                                                 "adjust"};
  for (unsigned i = 0; i < (unsigned)IFX_MCU_BOARD_ENCODER_COUNT; ++i) {
    if (strcmp(name, names[i]) == 0) {
      *encoder = (ifx_mcu_board_encoder)i;
      return true;
    }
  }
  return false;
}

static bool button_named(const char *name, ifx_mcu_board_button *button) {
  static const char *const names[IFX_MCU_BOARD_BUTTON_COUNT] = {"recall", "store", "compare",
                                                                "config", "bypass"};
  for (unsigned i = 0; i < (unsigned)IFX_MCU_BOARD_BUTTON_COUNT; ++i) {
    if (strcmp(name, names[i]) == 0) {
      *button = (ifx_mcu_board_button)i;
      return true;
    }
  }
  return false;
}

/* The factory presets that each of the eight DSP configurations runs on: the
 * images' own preset ranges, with the bypass presets sharing image 0. */
static const struct {
  unsigned preset;
  unsigned config;
} k_audio_configs[] = {{81u, 0u}, {86u, 1u}, {83u, 2u}, {84u, 3u}, {88u, 4u}, {82u, 5u}};

#define IFX_AUDIO_CONFIGS (sizeof(k_audio_configs) / sizeof(k_audio_configs[0]))

static bool audio_chunk(const int32_t *samples, size_t frames, int32_t *left, int32_t *right,
                        size_t *offset) {
  if (!feed(g_emu, samples, frames, true, left, right)) {
    printf("  the feed stopped at frame %llu\n", (unsigned long long)*offset);
    return false;
  }
  *offset += frames;
  return true;
}

/* A power cycle: the DSP's RAM keeps the delay lines, so a board reset alone
 * would let one run inherit the tail of the other. */
static const char *g_root;

static bool power_cycle(unsigned preset) {
  ifx_emu_destroy(g_emu);
  g_emu = ifx_emu_create(g_root);
  if (g_emu == NULL) {
    printf("cannot recreate the emulator\n");
    return false;
  }
  if (!ifx_emu_recall(g_emu, preset)) {
    printf("preset %u: recall refused\n", preset);
    return false;
  }
  ifx_emu_run_ms(g_emu, 500);
  return true;
}

/* One configuration: silence, an impulse for the echo spacing, then two
 * identical minutes of tone that must come out the same. */
static bool verify_one(unsigned preset, unsigned config) {
  int32_t left[4096];
  int32_t right[4096];
  size_t chunk = sizeof(left) / sizeof(left[0]);
  if (!power_cycle(preset)) {
    return false;
  }

  char text[IFX_MCU_BOARD_DISPLAY_WIDTH + 1u];
  ifx_emu_display(g_emu, text);
  printf("preset %u \"%s\": config %u (expected %u) page=%02x\n", preset, text,
         ifx_emu_config(g_emu), config, ifx_emu_page(g_emu));
  if (ifx_emu_config(g_emu) != config) {
    printf("  wrong configuration\n");
    return false;
  }

  size_t offset = 0;
  if (!audio_chunk(NULL, chunk, left, right, &offset)) {
    return false;
  }
  channel_stats silence_left = measure(left, chunk);
  channel_stats silence_right = measure(right, chunk);
  print_channel("silence L", &silence_left);
  print_channel("silence R", &silence_right);
  if (silence_left.peak > IFX_SILENCE_LIMIT || silence_right.peak > IFX_SILENCE_LIMIT) {
    printf("  silence is not quiet\n");
    return false;
  }

  int32_t *impulse = calloc(chunk, sizeof(*impulse));
  int32_t *impulse_left = calloc(chunk, sizeof(*impulse_left));
  int32_t *impulse_right = calloc(chunk, sizeof(*impulse_right));
  int32_t *tone = tone_samples(440.0, 25u);
  int32_t *tone_left = calloc(chunk, sizeof(*tone_left));
  int32_t *tone_right = calloc(chunk, sizeof(*tone_right));
  bool ok = impulse != NULL && impulse_left != NULL && impulse_right != NULL && tone != NULL &&
            tone_left != NULL && tone_right != NULL;
  if (ok) {
    impulse[0] = IFX_IMPULSE_AMPLITUDE;
    if (!audio_chunk(impulse, chunk, impulse_left, impulse_right, &offset)) {
      ok = false;
    } else {
      print_peaks(impulse_left, chunk, measure(impulse_left, chunk).mean);
    }
  }

  size_t tone_frames = frames_for_ms(25u);
  uint64_t first_run = 0;
  unsigned overruns = 0;
  unsigned underruns = 0;
  for (unsigned pass = 0; ok && pass < 2u; ++pass) {
    if (!power_cycle(preset)) {
      ok = false;
      break;
    }
    channel_stats left_run = {INT32_MAX, INT32_MIN, 0, 0, 0.0, 0xCBF29CE484222325u};
    channel_stats right_run = {INT32_MAX, INT32_MIN, 0, 0, 0.0, 0xCBF29CE484222325u};
    double squares = 0.0;
    double right_squares = 0.0;
    int64_t means = 0;
    int64_t right_means = 0;
    unsigned before_over = ifx_emu_overruns(g_emu);
    unsigned before_under = ifx_emu_underruns(g_emu);
    ifx_emu_messages_clear(g_emu);
    /* 2400 chunks of 25 ms: a minute a pass. */
    for (unsigned run_index = 0; run_index < 2400u; ++run_index) {
      if (!feed(g_emu, tone, tone_frames, true, tone_left, tone_right)) {
        printf("  the tone feed stopped at chunk %u\n", run_index);
        ok = false;
        break;
      }
      channel_stats piece = measure(tone_left, tone_frames);
      left_run.min = piece.min < left_run.min ? piece.min : left_run.min;
      left_run.max = piece.max > left_run.max ? piece.max : left_run.max;
      left_run.peak = piece.peak > left_run.peak ? piece.peak : left_run.peak;
      squares += piece.rms * piece.rms;
      means += piece.mean;
      left_run.checksum ^= piece.checksum * 0x100000001B3u;

      channel_stats right_piece = measure(tone_right, tone_frames);
      right_run.min = right_piece.min < right_run.min ? right_piece.min : right_run.min;
      right_run.max = right_piece.max > right_run.max ? right_piece.max : right_run.max;
      right_run.peak = right_piece.peak > right_run.peak ? right_piece.peak : right_run.peak;
      right_squares += right_piece.rms * right_piece.rms;
      right_means += right_piece.mean;
      right_run.checksum ^= right_piece.checksum * 0x100000001B3u;
    }
    if (!ok) {
      break;
    }
    left_run.mean = (int32_t)(means / 2400);
    left_run.rms = sqrt(squares / 2400.0);
    right_run.mean = (int32_t)(right_means / 2400);
    right_run.rms = sqrt(right_squares / 2400.0);
    printf("  pass %u over %zu frames:\n", pass + 1u, 2400u * tone_frames);
    print_channel("tone L", &left_run);
    print_channel("tone R", &right_run);
    printf("  overruns=%u underruns=%u\n", ifx_emu_overruns(g_emu) - before_over,
           ifx_emu_underruns(g_emu) - before_under);
    overruns += ifx_emu_overruns(g_emu) - before_over;
    underruns += ifx_emu_underruns(g_emu) - before_under;
    messages();
    uint64_t checksum = left_run.checksum ^ right_run.checksum;
    if (pass == 0u) {
      first_run = checksum;
    } else if (first_run != checksum) {
      printf("  the two runs differ\n");
      ok = false;
    }
  }
  free(impulse);
  free(impulse_left);
  free(impulse_right);
  free(tone);
  free(tone_left);
  free(tone_right);
  return ok && overruns == 0u && underruns == 0u;
}

/* Preset 84's DELAY page: the firmware stores 196 samples of delay per step of
 * the time parameter, and the 2-tap echo repeats at that spacing. */
#define IFX_ECHO_PRESET 84u
#define IFX_ECHO_PARAMETER 0x1Du
#define IFX_ECHO_SAMPLES_PER_UNIT 196u

static bool verify_echo(void) {
  static const unsigned k_values[] = {40u, 80u};
  size_t window = frames_for_ms(1000u);
  int32_t *impulse = calloc(window, sizeof(*impulse));
  int32_t *left = calloc(window, sizeof(*left));
  int32_t *right = calloc(window, sizeof(*right));
  bool ok = impulse != NULL && left != NULL && right != NULL;
  size_t latency = 0;
  for (unsigned i = 0; ok && i < sizeof(k_values) / sizeof(k_values[0]); ++i) {
    unsigned value = k_values[i];
    size_t spacing = IFX_ECHO_SAMPLES_PER_UNIT * value;
    if (!ifx_emu_recall(g_emu, IFX_ECHO_PRESET) ||
        !ifx_emu_set_parameter(g_emu, IFX_ECHO_PARAMETER, value)) {
      printf("echo %u: the panel refused\n", value);
      ok = false;
      break;
    }
    ifx_emu_run_ms(g_emu, 500);
    impulse[0] = IFX_IMPULSE_AMPLITUDE;
    size_t offset = 0;
    if (!audio_chunk(impulse, window, left, right, &offset)) {
      ok = false;
      break;
    }
    int32_t mean = measure(left, window).mean;
    print_peaks(left, window, mean);
    size_t positions[12];
    int32_t values[12];
    size_t count = collect_peaks(left, window, mean, positions, values, 12u);
    if (count < 2u) {
      printf("echo %u: %zu echoes in %zu frames, expected at least 2\n", value, count, window);
      ok = false;
      break;
    }
    for (size_t p = 1; p < count; ++p) {
      if (positions[p] - positions[p - 1u] != spacing) {
        printf("echo %u: echoes %zu and %zu are %zu frames apart, expected %zu\n", value, p - 1u, p,
               positions[p] - positions[p - 1u], spacing);
        ok = false;
        break;
      }
    }
    size_t first = positions[0] - spacing;
    if (latency == 0u) {
      latency = first;
    } else if (latency != first) {
      printf("echo %u: latency %zu frames, was %zu\n", value, first, latency);
      ok = false;
      break;
    }
  }
  if (ok) {
    printf("  %u samples per step, %zu frames of pipeline latency\n", IFX_ECHO_SAMPLES_PER_UNIT,
           latency);
  }

  /* A turn of the knob while audio runs must reach the DSP on its own. */
  unsigned value = ifx_emu_value(g_emu, IFX_ECHO_PARAMETER);
  ifx_emu_messages_clear(g_emu);
  ifx_emu_turn(g_emu, IFX_MCU_BOARD_PARAMETER_ADJUST, 3);
  unsigned moved = ifx_emu_value(g_emu, IFX_ECHO_PARAMETER);
  unsigned count = 0;
  (void)ifx_emu_messages(g_emu, &count);
  printf("  three detents took %u to %u and sent %u messages\n", value, moved, count);
  if (ok && (moved != value + 3u || count == 0u)) {
    printf("  the turn did not reach the panel and the DSP\n");
    ok = false;
  }
  free(impulse);
  free(left);
  free(right);
  return ok;
}

static bool verify_audio(void) {
  bool ok = true;
  for (unsigned i = 0; i < IFX_AUDIO_CONFIGS; ++i) {
    if (!verify_one(k_audio_configs[i].preset, k_audio_configs[i].config)) {
      printf("configuration %u FAILED\n", k_audio_configs[i].config);
      ok = false;
    } else {
      printf("configuration %u ok\n", k_audio_configs[i].config);
    }
  }
  return ok;
}

static bool verify_factory(void) {
  uint8_t reference[IFX_MCU_BOARD_BATTERY_SIZE];
  ifx_emu_battery_factory(g_emu);
  memcpy(reference, ifx_emu_battery(g_emu), sizeof(reference));
  bool reported = ifx_emu_factory_restore(g_emu);

  char text[IFX_MCU_BOARD_DISPLAY_WIDTH + 1u];
  ifx_emu_display(g_emu, text);
  printf("  display \"%s\"\n", text);
  if (!reported) {
    printf("  the restore did not report zero errors\n");
  }
  const uint8_t *battery = ifx_emu_battery(g_emu);
  size_t differences = 0;
  size_t first = 0;
  for (size_t i = 0; i < sizeof(reference); ++i) {
    if (battery[i] != reference[i]) {
      if (differences == 0u) {
        first = i;
      }
      ++differences;
    }
  }
  if (differences == 0u) {
    printf("  the panel's restore matches the factory battery\n");
    return reported;
  }
  printf("  %zu bytes differ, the first at %04zx: %02x, the factory %02x\n", differences, first,
         (unsigned)battery[first], (unsigned)reference[first]);
  return false;
}

#define IFX_TIMING_REST 0x066666
#define IFX_TIMING_IMPULSE 0x400000
#define IFX_TIMING_LATENCY 64u
#define IFX_TIMING_THRESHOLD 100000
#define IFX_TIMING_SPACING 4096u
#define IFX_TIMING_RUN_FRAMES 768u
#define IFX_TIMING_SETTLE_RUN 512u
#define IFX_TIMING_MAX_TABLE 0x12C8u
#define IFX_TIMING_PARAMETER_LISTS 0x320Fu
#define IFX_TIMING_FACTORY_PRESETS 0x2084u

static uint8_t g_timing_max[IFX_EMU_PARAMETER_COUNT];
static bool g_timing_max_valid;

static bool timing_eprom_read(unsigned offset, uint8_t *data, size_t size) {
  FILE *file = fopen(ifx_emu_eprom_path(g_emu), "rb");
  if (file == NULL) {
    return false;
  }
  bool ok = fseek(file, (long)offset, SEEK_SET) == 0 && fread(data, 1u, size, file) == size;
  fclose(file);
  return ok;
}

static unsigned timing_maximum(unsigned index) {
  if (!g_timing_max_valid) {
    g_timing_max_valid =
        timing_eprom_read(IFX_TIMING_MAX_TABLE, g_timing_max, sizeof(g_timing_max));
  }
  return g_timing_max_valid && index < IFX_EMU_PARAMETER_COUNT ? g_timing_max[index] : 0u;
}

static bool timing_factory_values(unsigned config, uint8_t values[IFX_EMU_PARAMETER_COUNT]) {
  if (config >= IFX_AUDIO_CONFIGS) {
    return false;
  }
  uint8_t list[64];
  uint8_t record[55];
  unsigned preset = k_audio_configs[config].preset;
  if (!timing_eprom_read(IFX_TIMING_PARAMETER_LISTS + 64u * config, list, sizeof(list)) ||
      !timing_eprom_read(IFX_TIMING_FACTORY_PRESETS + 55u * (preset - 81u), record,
                         sizeof(record))) {
    return false;
  }
  memset(values, 0, IFX_EMU_PARAMETER_COUNT);
  unsigned position = 1u;
  for (unsigned i = 0; i < sizeof(list) && list[i] != 0u; ++i) {
    if (list[i] >= IFX_EMU_PARAMETER_COUNT || position >= sizeof(record)) {
      return false;
    }
    values[list[i]] = record[position++];
  }
  return true;
}

static bool timing_direct_values(unsigned config, uint8_t values[IFX_EMU_PARAMETER_COUNT]) {
  uint8_t list[64];
  if (!timing_factory_values(config, values) ||
      !timing_eprom_read(IFX_TIMING_PARAMETER_LISTS + 64u * config, list, sizeof(list))) {
    return false;
  }
  for (unsigned i = 0; i < sizeof(list) && list[i] != 0u; ++i) {
    unsigned index = list[i];
    if (index == 2u || (index >= 6u && index <= 8u) || index == 45u ||
        (index >= 50u && index <= 53u)) {
      values[index] = 0;
    }
  }
  values[9] = 0;
  values[5] = 1;
  values[3] = (uint8_t)(timing_maximum(3) - 1u);
  values[4] = (uint8_t)(timing_maximum(4) - 1u);
  return true;
}

static bool timing_apply(unsigned config, const uint8_t values[IFX_EMU_PARAMETER_COUNT],
                         uint64_t *settle_frames) {
  if (!ifx_emu_load(g_emu, config, values)) {
    printf("config %u: preset load refused\n", config);
    return false;
  }
  if (!ifx_emu_send(g_emu, 2u, 0u)) {
    printf("config %u: effect level send refused\n", config);
    return false;
  }
  if (!ifx_emu_settle(g_emu, settle_frames)) {
    printf("config %u: settle refused (preset %u config %u flags %06x ptr %06x frames %llu)\n",
           config, ifx_emu_preset(g_emu), ifx_emu_config(g_emu),
           ifx_dsp_board_word(ifx_emu_dsp(g_emu), DSP_SPACE_X, 0u),
           ifx_dsp_board_word(ifx_emu_dsp(g_emu), DSP_SPACE_X, 0x17u),
           (unsigned long long)ifx_emu_frames(g_emu));
    return false;
  }
  return true;
}

static size_t timing_first_deviation(const int32_t *left, const int32_t *right, size_t frames,
                                     int32_t threshold) {
  for (size_t i = 0; i < frames; ++i) {
    int64_t dl = (int64_t)left[i] - IFX_TIMING_REST;
    int64_t dr = (int64_t)right[i] - IFX_TIMING_REST;
    if ((dl < 0 ? -dl : dl) > threshold || (dr < 0 ? -dr : dr) > threshold) {
      return i;
    }
  }
  return frames;
}

static bool timing_block_rest(const int32_t *samples, size_t start) {
  for (size_t i = 0; i < 32u; ++i) {
    if (samples[start + i] != IFX_TIMING_REST) {
      return false;
    }
  }
  return true;
}

static void timing_sine(int32_t *samples, size_t frames, double hz, double phase) {
  for (size_t i = 0; i < frames; ++i) {
    double angle = 2.0 * 3.14159265358979323846 * hz * (double)i / IFX_SAMPLE_RATE_HZ + phase;
    samples[i] = (int32_t)(2000000.0 * sin(angle));
  }
}

static bool timing_latency(unsigned config, uint64_t *settle_frames) {
  uint8_t values[IFX_EMU_PARAMETER_COUNT];
  if (!timing_direct_values(config, values)) {
    printf("config %u latency: values refused\n", config);
    return false;
  }
  if (!timing_apply(config, values, settle_frames)) {
    printf("config %u latency: load or settle refused\n", config);
    return false;
  }
  if (!ifx_emu_align(g_emu)) {
    printf("config %u latency: align refused\n", config);
    return false;
  }

  int32_t *input = calloc(IFX_TIMING_SPACING, sizeof(*input));
  int32_t *left = calloc(IFX_TIMING_SPACING, sizeof(*left));
  int32_t *right = calloc(IFX_TIMING_SPACING, sizeof(*right));
  bool ok = input != NULL && left != NULL && right != NULL;
  for (unsigned phase = 0; ok && phase < 32u; ++phase) {
    memset(input, 0, IFX_TIMING_SPACING * sizeof(*input));
    input[phase] = IFX_TIMING_IMPULSE;
    ifx_emu_feed request = {input, IFX_TIMING_SPACING, false};
    if (!ifx_emu_audio(g_emu, &request, left, right)) {
      ok = false;
      break;
    }
    size_t first = timing_first_deviation(left, right, IFX_TIMING_SPACING, IFX_TIMING_THRESHOLD);
    if (first != phase + IFX_TIMING_LATENCY) {
      printf("config %u latency: phase %u first appears at %zu, expected %u\n", config, phase,
             first, phase + IFX_TIMING_LATENCY);
      ok = false;
    }
  }
  free(input);
  free(left);
  free(right);

  for (unsigned kind = 0; ok && kind < 2u; ++kind) {
    if (!timing_apply(config, values, settle_frames)) {
      printf("config %u %s latency: load or settle refused\n", config,
             kind == 0u ? "sine" : "noise");
      ok = false;
      break;
    }
    if (!ifx_emu_align(g_emu)) {
      printf("config %u %s latency: align refused\n", config, kind == 0u ? "sine" : "noise");
      ok = false;
      break;
    }
    int32_t *signal = calloc(IFX_TIMING_SETTLE_RUN, sizeof(*signal));
    int32_t *out_left = calloc(IFX_TIMING_SETTLE_RUN, sizeof(*out_left));
    int32_t *out_right = calloc(IFX_TIMING_SETTLE_RUN, sizeof(*out_right));
    if (signal == NULL || out_left == NULL || out_right == NULL) {
      free(signal);
      free(out_left);
      free(out_right);
      ok = false;
      break;
    }
    if (kind == 0u) {
      timing_sine(signal, IFX_TIMING_SETTLE_RUN, 440.0, 1.57079632679489661923);
    } else {
      uint32_t state = 0x12345678u;
      for (size_t i = 0; i < IFX_TIMING_SETTLE_RUN; ++i) {
        state = state * 1664525u + 1013904223u;
        signal[i] = (int32_t)(state & 0x7FFFFFu) - 0x400000;
      }
    }
    ifx_emu_feed request = {signal, IFX_TIMING_SETTLE_RUN, false};
    if (!ifx_emu_audio(g_emu, &request, out_left, out_right)) {
      printf("config %u %s latency: feed refused\n", config, kind == 0u ? "sine" : "noise");
      ok = false;
    } else {
      size_t first = timing_first_deviation(out_left, out_right, IFX_TIMING_SETTLE_RUN, 1000);
      if (first != IFX_TIMING_LATENCY) {
        printf("config %u %s latency first appears at %zu, expected %u\n", config,
               kind == 0u ? "sine" : "noise", first, IFX_TIMING_LATENCY);
        ok = false;
      }
    }
    free(signal);
    free(out_left);
    free(out_right);
  }
  if (!ok) {
    printf("config %u latency: FAILED\n", config);
  }
  return ok;
}

static bool timing_message(unsigned config, uint64_t *settle_frames) {
  uint8_t values[IFX_EMU_PARAMETER_COUNT];
  if (!timing_direct_values(config, values) || !timing_apply(config, values, settle_frames)) {
    printf("config %u messages: setup failed\n", config);
    return false;
  }
  int32_t *input = calloc(IFX_TIMING_RUN_FRAMES, sizeof(*input));
  int32_t *left = calloc(IFX_TIMING_RUN_FRAMES, sizeof(*left));
  int32_t *right = calloc(IFX_TIMING_RUN_FRAMES, sizeof(*right));
  bool ok = input != NULL && left != NULL && right != NULL;
  unsigned maximum = timing_maximum(3) - 1u;

  for (unsigned phase = 0; ok && phase < 32u; ++phase) {
    if (!ifx_emu_align(g_emu)) {
      ok = false;
      break;
    }
    ifx_emu_messages_clear(g_emu);
    uint64_t pass_base = ifx_emu_blocks(g_emu);
    uint64_t frame_base = ifx_emu_frames(g_emu);
    timing_sine(input, IFX_TIMING_RUN_FRAMES, 440.0, 1.57079632679489661923);

    size_t frame = 0;
    unsigned turn = 0;
    while (frame < IFX_TIMING_RUN_FRAMES) {
      if (turn < 10u && frame == 32u + phase + 64u * turn) {
        unsigned value = (turn & 1u) != 0u ? maximum : 0u;
        if (!ifx_emu_send(g_emu, 3u, value)) {
          printf("config %u messages: send failed at phase %u turn %u\n", config, phase, turn);
          ok = false;
          break;
        }
        ++turn;
        continue;
      }
      size_t next = turn < 10u ? 32u + phase + 64u * turn : IFX_TIMING_RUN_FRAMES;
      if (next <= frame) {
        next = frame + 1u;
      }
      size_t chunk = next - frame;
      ifx_emu_feed request = {input + frame, chunk, false};
      if (!ifx_emu_audio(g_emu, &request, left + frame, right + frame)) {
        ok = false;
        break;
      }
      frame += chunk;
    }
    if (!ok) {
      break;
    }
    if (turn != 10u) {
      printf("config %u messages: only %u turns ran at phase %u\n", config, turn, phase);
      ok = false;
      break;
    }

    unsigned count = 0;
    const ifx_emu_message *log = ifx_emu_messages(g_emu, &count);
    if (count != 10u) {
      printf("config %u messages: %u logged at phase %u, expected 10\n", config, count, phase);
      ok = false;
      break;
    }
    for (unsigned k = 0; k < 10u; ++k) {
      unsigned value = (k & 1u) != 0u ? maximum : 0u;
      unsigned block = 1u + 2u * k;
      uint64_t expected_frame = frame_base + 32u + phase + 64u * k;
      uint64_t expected_pass = pass_base + 1u + block;
      if (log[k].index != 3u || log[k].value != value || log[k].frame_in != expected_frame ||
          log[k].pass_applied != expected_pass) {
        printf("config %u messages: phase %u turn %u log index=%u value=%u frame=%llu pass=%llu,"
               " expected 3/%u/%llu/%llu\n",
               config, phase, k, log[k].index, log[k].value, (unsigned long long)log[k].frame_in,
               (unsigned long long)log[k].pass_applied, value, (unsigned long long)expected_frame,
               (unsigned long long)expected_pass);
        ok = false;
        break;
      }
      bool want_rest = (k & 1u) == 0u;
      size_t start = 64u + 32u * block;
      bool got_rest = timing_block_rest(left, start);
      bool next_rest = timing_block_rest(left, start + 32u);
      bool previous_rest = timing_block_rest(left, start - 32u);
      if (got_rest != want_rest || next_rest != want_rest || previous_rest == want_rest) {
        printf("config %u messages: phase %u turn %u output block %u rest=%d next=%d previous=%d,"
               " expected %d\n",
               config, phase, k, block, (int)got_rest, (int)next_rest, (int)previous_rest,
               (int)want_rest);
        ok = false;
        break;
      }
    }
  }

  if (ok) {
    if (!ifx_emu_align(g_emu)) {
      ok = false;
    } else {
      ifx_emu_messages_clear(g_emu);
      uint64_t pass_base = ifx_emu_blocks(g_emu);
      uint64_t frame_base = ifx_emu_frames(g_emu);
      timing_sine(input, IFX_TIMING_RUN_FRAMES, 440.0, 1.57079632679489661923);
      ifx_emu_feed first = {input, 32u, false};
      ok = ifx_emu_audio(g_emu, &first, left, right);
      if (ok && (!ifx_emu_send(g_emu, 3u, 0u) || !ifx_emu_send(g_emu, 4u, 0u))) {
        ok = false;
      }
      ifx_emu_feed rest = {input + 32u, 96u, false};
      if (ok) {
        ok = ifx_emu_audio(g_emu, &rest, left + 32u, right + 32u);
      }
      unsigned count = 0;
      const ifx_emu_message *log = ifx_emu_messages(g_emu, &count);
      uint64_t expected_pass = pass_base + 2u;
      if (ok && (count != 2u || log[0].index != 3u || log[1].index != 4u || log[0].value != 0u ||
                 log[1].value != 0u || log[0].frame_in != frame_base + 32u ||
                 log[1].frame_in != frame_base + 32u || log[0].pass_applied != expected_pass ||
                 log[1].pass_applied != expected_pass || !timing_block_rest(left, 96u) ||
                 !timing_block_rest(right, 96u))) {
        printf("config %u messages: two in a row failed\n", config);
        ok = false;
      }
    }
  }

  free(input);
  free(left);
  free(right);
  if (!ok) {
    printf("config %u messages: FAILED\n", config);
  }
  return ok;
}

static uint64_t timing_hash(const int32_t *left, const int32_t *right, size_t frames) {
  uint64_t hash = 0xCBF29CE484222325u;
  for (size_t i = 0; i < frames; ++i) {
    hash ^= (uint32_t)left[i];
    hash *= 0x100000001B3u;
    hash ^= (uint32_t)right[i];
    hash *= 0x100000001B3u;
  }
  return hash;
}

static uint32_t timing_parameter_checksum(void) {
  uint32_t checksum = 2166136261u;
  for (unsigned i = 0; i < 0x2Cu; ++i) {
    checksum ^= ifx_dsp_board_word(ifx_emu_dsp(g_emu), DSP_SPACE_Y, i);
    checksum *= 16777619u;
  }
  return checksum;
}

static bool timing_load_settle(unsigned config, uint64_t *settle_frames) {
  uint8_t values[IFX_EMU_PARAMETER_COUNT];
  uint8_t list[64];
  if (!timing_factory_values(config, values) ||
      !timing_eprom_read(IFX_TIMING_PARAMETER_LISTS + 64u * config, list, sizeof(list))) {
    return false;
  }
  unsigned first = list[0];
  values[first] = (uint8_t)(timing_maximum(first) / 2u);

  uint32_t checksums[2];
  uint64_t hashes[2];
  uint64_t settles[2];
  int32_t *signal = calloc(IFX_TIMING_SETTLE_RUN, sizeof(*signal));
  int32_t *left = calloc(IFX_TIMING_SETTLE_RUN, sizeof(*left));
  int32_t *right = calloc(IFX_TIMING_SETTLE_RUN, sizeof(*right));
  bool ok = signal != NULL && left != NULL && right != NULL;
  if (ok) {
    timing_sine(signal, IFX_TIMING_SETTLE_RUN, 1000.0, 0.52359877559829887308);
  }
  for (unsigned run = 0; ok && run < 2u; ++run) {
    if (!ifx_emu_load(g_emu, config, values) || ifx_emu_config(g_emu) != config) {
      ok = false;
      break;
    }
    for (unsigned i = 0; i < sizeof(list) && list[i] != 0u; ++i) {
      if (ifx_emu_value(g_emu, list[i]) != values[list[i]]) {
        printf("config %u load: parameter %u is %u, expected %u\n", config, list[i],
               ifx_emu_value(g_emu, list[i]), values[list[i]]);
        ok = false;
        break;
      }
    }
    if (!ifx_emu_settle(g_emu, &settles[run])) {
      ok = false;
      break;
    }
    checksums[run] = timing_parameter_checksum();
    if (checksums[run] == 0u) {
      printf("config %u load: DSP parameter words are empty\n", config);
      ok = false;
      break;
    }
    if (!ifx_emu_align(g_emu)) {
      ok = false;
      break;
    }
    ifx_emu_feed request = {signal, IFX_TIMING_SETTLE_RUN, false};
    if (!ifx_emu_audio(g_emu, &request, left, right)) {
      ok = false;
      break;
    }
    hashes[run] = timing_hash(left, right, IFX_TIMING_SETTLE_RUN);
  }
  free(signal);
  free(left);
  free(right);
  if (ok && (checksums[0] != checksums[1] || hashes[0] != hashes[1])) {
    printf("config %u load: the two runs differ\n", config);
    ok = false;
  }
  if (ok && settles[0] != settles[1]) {
    printf("config %u settle: %llu and %llu frames\n", config, (unsigned long long)settles[0],
           (unsigned long long)settles[1]);
    ok = false;
  }
  if (ok) {
    *settle_frames = settles[0];
  } else {
    printf("config %u load and settle: FAILED\n", config);
  }
  return ok;
}

typedef struct {
  unsigned config;
  unsigned preset;
  uint64_t settle;
  uint64_t settle_load;
} timing_row;

static bool verify_timing(void) {
  timing_row rows[IFX_AUDIO_CONFIGS];
  for (unsigned i = 0; i < IFX_AUDIO_CONFIGS; ++i) {
    unsigned config = k_audio_configs[i].config;
    uint64_t settle_latency = 0;
    uint64_t settle_messages = 0;
    uint64_t settle_load = 0;
    bool ok = timing_latency(config, &settle_latency);
    if (!ok) {
      printf("timing FAILED: configuration %u latency\n", config);
      return false;
    }
    ok = timing_message(config, &settle_messages);
    if (!ok) {
      printf("timing FAILED: configuration %u messages\n", config);
      return false;
    }
    ok = timing_load_settle(config, &settle_load);
    if (!ok) {
      printf("timing FAILED: configuration %u load and settle\n", config);
      return false;
    }
    rows[i].config = config;
    rows[i].preset = k_audio_configs[i].preset;
    rows[i].settle = settle_messages;
    rows[i].settle_load = settle_load;
  }
  printf("config preset  latency  phases  messages  turns  settle\n");
  for (unsigned i = 0; i < IFX_AUDIO_CONFIGS; ++i) {
    printf("%6u %6u %8u %7s %9s %6s %6llu\n", rows[i].config, rows[i].preset, IFX_TIMING_LATENCY,
           "0-31", "0-31", "10x64", (unsigned long long)rows[i].settle_load);
  }
  (void)rows[0].settle;
  printf("timing ok\n");
  return true;
}

static bool command_load(const char *text) {
  char *end = NULL;
  unsigned long parsed = strtoul(text, &end, 10);
  if (end == text || *end != ':' || parsed >= IFX_AUDIO_CONFIGS) {
    return false;
  }
  uint8_t values[IFX_EMU_PARAMETER_COUNT];
  if (!timing_factory_values((unsigned)parsed, values)) {
    return false;
  }
  const char *at = end + 1;
  while (*at != '\0') {
    unsigned long index = strtoul(at, &end, 10);
    if (end == at || *end != '=' || index >= IFX_EMU_PARAMETER_COUNT) {
      return false;
    }
    at = end + 1;
    unsigned long value = strtoul(at, &end, 10);
    if (end == at || value > 255u) {
      return false;
    }
    values[index] = (uint8_t)value;
    at = end;
    if (*at == ',') {
      ++at;
    } else if (*at != '\0') {
      return false;
    }
  }
  return ifx_emu_load(g_emu, (unsigned)parsed, values);
}

enum { IFX_CMD_OK = 0, IFX_CMD_FAILED = 1, IFX_CMD_UNKNOWN = 2 };

static int command(const char *arg) {
  if (arg[0] >= '0' && arg[0] <= '9') {
    run_ms((unsigned)strtoul(arg, NULL, 10));
    return IFX_CMD_OK;
  }
  if (strcmp(arg, "show") == 0) {
    show();
    return IFX_CMD_OK;
  }
  if (strcmp(arg, "dump") == 0) {
    dump();
    return IFX_CMD_OK;
  }
  if (strcmp(arg, "messages") == 0) {
    messages();
    return IFX_CMD_OK;
  }
  if (strcmp(arg, "factory") == 0) {
    ifx_emu_battery_factory(g_emu);
    ifx_emu_reset(g_emu);
    printf("  battery rebuilt from the firmware's restore tables\n");
    return IFX_CMD_OK;
  }
  if (strcmp(arg, "erase") == 0) {
    ifx_emu_battery_erase(g_emu);
    ifx_emu_reset(g_emu);
    printf("  battery erased\n");
    return IFX_CMD_OK;
  }
  if (strncmp(arg, "send:", 5) == 0) {
    unsigned index = 0;
    unsigned value = 0;
    if (sscanf(arg + 5, "%u=%u", &index, &value) != 2) {
      return IFX_CMD_FAILED;
    }
    bool ok = ifx_emu_send(g_emu, index, value);
    printf("  send %u=%u: %s\n", index, value, ok ? "ok" : "refused");
    return ok ? IFX_CMD_OK : IFX_CMD_FAILED;
  }
  if (strcmp(arg, "align") == 0) {
    bool ok = ifx_emu_align(g_emu);
    printf("  align: %s, phase %u, block %llu\n", ok ? "ok" : "failed", ifx_emu_block_phase(g_emu),
           (unsigned long long)ifx_emu_blocks(g_emu));
    return ok ? IFX_CMD_OK : IFX_CMD_FAILED;
  }
  if (strcmp(arg, "phase") == 0) {
    printf("  phase %u, block %llu\n", ifx_emu_block_phase(g_emu),
           (unsigned long long)ifx_emu_blocks(g_emu));
    return IFX_CMD_OK;
  }
  if (strcmp(arg, "settle") == 0) {
    uint64_t frames = 0;
    bool ok = ifx_emu_settle(g_emu, &frames);
    printf("  settle: %s, %llu frames\n", ok ? "ok" : "failed", (unsigned long long)frames);
    return ok ? IFX_CMD_OK : IFX_CMD_FAILED;
  }
  if (strcmp(arg, "verify:timing") == 0) {
    bool ok = verify_timing();
    return ok ? IFX_CMD_OK : IFX_CMD_FAILED;
  }
  if (strcmp(arg, "verify:echo") == 0) {
    if (verify_echo()) {
      printf("echo ok\n");
      return IFX_CMD_OK;
    }
    printf("echo FAILED\n");
    return IFX_CMD_FAILED;
  }
  if (strcmp(arg, "verify:audio") == 0) {
    bool ok = verify_audio();
    printf("audio verification: %s\n", ok ? "ok" : "FAILED");
    return IFX_CMD_OK;
  }
  if (strcmp(arg, "verify:factory") == 0) {
    bool ok = verify_factory();
    printf("factory restore: %s\n", ok ? "ok" : "FAILED");
    return IFX_CMD_OK;
  }
  if (strncmp(arg, "save:", 5) == 0) {
    bool ok = ifx_emu_battery_save(g_emu, arg + 5);
    printf("  battery saved to %s: %s\n", arg + 5, ok ? "ok" : "failed");
    return ok ? IFX_CMD_OK : IFX_CMD_FAILED;
  }
  if (strncmp(arg, "load:", 5) == 0) {
    if (arg[5] >= '0' && arg[5] <= '9') {
      bool ok = command_load(arg + 5);
      printf("  preset load: %s\n", ok ? "ok" : "refused");
      return ok ? IFX_CMD_OK : IFX_CMD_FAILED;
    }
    bool ok = ifx_emu_battery_load(g_emu, arg + 5);
    printf("  battery loaded from %s: %s\n", arg + 5, ok ? "ok" : "failed");
    if (ok) {
      ifx_emu_reset(g_emu);
    }
    return ok ? IFX_CMD_OK : IFX_CMD_FAILED;
  }
  if (strncmp(arg, "recall:", 7) == 0) {
    unsigned preset = (unsigned)strtoul(arg + 7, NULL, 10);
    bool ok = ifx_emu_recall(g_emu, preset);
    printf("  recall %u: %s\n", preset, ok ? "ok" : "refused");
    return ok ? IFX_CMD_OK : IFX_CMD_FAILED;
  }
  if (strncmp(arg, "set:", 4) == 0) {
    unsigned index = 0;
    unsigned value = 0;
    if (sscanf(arg + 4, "%u=%u", &index, &value) != 2) {
      return IFX_CMD_FAILED;
    }
    bool ok = ifx_emu_set_parameter(g_emu, index, value);
    printf("  parameter %u: %s, reads %u\n", index, ok ? "set" : "refused",
           ifx_emu_value(g_emu, index));
    return ok ? IFX_CMD_OK : IFX_CMD_FAILED;
  }
  if (strncmp(arg, "silence:", 8) == 0 || strncmp(arg, "impulse:", 8) == 0) {
    size_t frames = frames_for_ms((unsigned)strtoul(arg + 8, NULL, 10));
    if (frames == 0u) {
      return IFX_CMD_FAILED;
    }
    int32_t *input = calloc(frames, sizeof(*input));
    int32_t *left = calloc(frames, sizeof(*left));
    int32_t *right = calloc(frames, sizeof(*right));
    bool ok = input != NULL && left != NULL && right != NULL;
    if (ok) {
      if (arg[0] == 'i') {
        input[0] = 0x7FFFFF;
      }
      ok = feed(g_emu, input, frames, true, left, right);
      channel_stats left_stats = measure(left, frames);
      channel_stats right_stats = measure(right, frames);
      print_channel("L", &left_stats);
      print_channel("R", &right_stats);
      if (arg[0] == 'i') {
        print_peaks(left, frames, left_stats.mean);
      }
    }
    free(input);
    free(left);
    free(right);
    return ok ? IFX_CMD_OK : IFX_CMD_FAILED;
  }
  if (strncmp(arg, "tone:", 5) == 0) {
    double hz = 440.0;
    unsigned ms = 1000;
    if (sscanf(arg + 5, "%lf:%u", &hz, &ms) < 1) {
      return IFX_CMD_FAILED;
    }
    size_t frames = frames_for_ms(ms);
    int32_t *input = tone_samples(hz, ms);
    int32_t *left = calloc(frames, sizeof(*left));
    int32_t *right = calloc(frames, sizeof(*right));
    bool ok = input != NULL && left != NULL && right != NULL;
    if (ok) {
      ok = feed(g_emu, input, frames, true, left, right);
      channel_stats left_stats = measure(left, frames);
      channel_stats right_stats = measure(right, frames);
      print_channel("L", &left_stats);
      print_channel("R", &right_stats);
    }
    free(input);
    free(left);
    free(right);
    return ok ? IFX_CMD_OK : IFX_CMD_FAILED;
  }
  if (strncmp(arg, "turn:", 5) == 0 || strncmp(arg, "button:", 7) == 0 ||
      strncmp(arg, "press:", 6) == 0 || strncmp(arg, "release:", 8) == 0) {
    char name[16];
    const char *rest = strchr(arg, ':');
    rest = rest != NULL ? rest + 1 : arg;
    const char *colon = strchr(rest, ':');
    size_t length = colon != NULL ? (size_t)(colon - rest) : strlen(rest);
    if (length >= sizeof(name)) {
      return IFX_CMD_FAILED;
    }
    memcpy(name, rest, length);
    name[length] = '\0';

    if (arg[0] == 't') {
      ifx_mcu_board_encoder encoder = IFX_MCU_BOARD_PRESET;
      if (!encoder_named(name, &encoder)) {
        return IFX_CMD_FAILED;
      }
      ifx_emu_turn(g_emu, encoder, colon != NULL ? (int)strtol(colon + 1, NULL, 10) : 1);
      return IFX_CMD_OK;
    }
    ifx_mcu_board_button button = IFX_MCU_BOARD_BUTTON_RECALL;
    if (!button_named(name, &button)) {
      return IFX_CMD_FAILED;
    }
    if (arg[0] == 'b') {
      press(button);
      run_ms(30);
      release(button);
      run_ms(50);
    } else if (arg[0] == 'p') {
      press(button);
    } else {
      release(button);
    }
    return IFX_CMD_OK;
  }
  return IFX_CMD_UNKNOWN;
}

int main(int argc, char **argv) {
  g_emu = ifx_emu_create(g_root);
  if (g_emu == NULL) {
    return 1;
  }
  printf("EPROM %s, boot %llu us\n", ifx_emu_eprom_path(g_emu),
         (unsigned long long)(ifx_emu_clocks(g_emu) / IFX_MCU_BOARD_CLOCKS_PER_CYCLE));
  for (int i = 1; i < argc; ++i) {
    int result = command(argv[i]);
    if (result != IFX_CMD_OK) {
      if (result == IFX_CMD_UNKNOWN) {
        fprintf(stderr, "unknown command %s\n", argv[i]);
      }
      ifx_emu_destroy(g_emu);
      return 1;
    }
  }
  if (argc == 1) {
    show();
  }
  ifx_emu_destroy(g_emu);
  return 0;
}
