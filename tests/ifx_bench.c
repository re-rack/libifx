/* The benchmark: runs each effect on Q23 test-signal buffers and reports the
 * median processing time of seven 2 s runs in nanoseconds per frame. */
#define _POSIX_C_SOURCE 200809L

#include "ifx_effect.h"
#include "ifx_signal.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

enum {
  BENCH_BUFFER = 128,
  BENCH_SIGNAL_FRAMES = 65536,
  BENCH_WARMUP_FRAMES = 48000,
  BENCH_RUN_FRAMES = 78000,
  BENCH_RUNS = 7,
};

static int bench_now_ns(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (int)((int64_t)now.tv_sec * 1000000000LL + now.tv_nsec);
}

static void bench_run(void *instance, const ifx_effect *effect, const int32_t *in, size_t frames,
                      size_t *at) {
  int32_t left[BENCH_BUFFER];
  int32_t right[BENCH_BUFFER];
  while (frames != 0u) {
    size_t chunk = frames < BENCH_BUFFER ? frames : BENCH_BUFFER;
    if (chunk > (size_t)BENCH_SIGNAL_FRAMES - *at) {
      chunk = (size_t)BENCH_SIGNAL_FRAMES - *at;
    }
    effect->split(instance, in + *at, left, right, chunk);
    *at = (*at + chunk) % (size_t)BENCH_SIGNAL_FRAMES;
    frames -= chunk;
  }
}

static double bench_measure(const ifx_effect *effect, const int32_t *values, const int32_t *in) {
  void *instance = effect->open(values);
  if (instance == NULL) {
    return -1.0;
  }
  size_t at = 0;
  bench_run(instance, effect, in, BENCH_WARMUP_FRAMES, &at);
  double times[BENCH_RUNS];
  for (unsigned run = 0; run < BENCH_RUNS; run++) {
    const int start = bench_now_ns();
    bench_run(instance, effect, in, BENCH_RUN_FRAMES, &at);
    const int stop = bench_now_ns();
    times[run] = (double)(stop - start) / (double)BENCH_RUN_FRAMES;
  }
  effect->close(instance);
  for (unsigned i = 0; i < BENCH_RUNS; i++) {
    for (unsigned j = i + 1; j < BENCH_RUNS; j++) {
      if (times[j] < times[i]) {
        const double swap = times[i];
        times[i] = times[j];
        times[j] = swap;
      }
    }
  }
  return times[BENCH_RUNS / 2u];
}

static const ifx_effect *find_effect(const char *name) {
  if (strcmp(name, ifx_effect_delay.name) == 0) {
    return &ifx_effect_delay;
  }
  if (strcmp(name, ifx_effect_chorus8.name) == 0) {
    return &ifx_effect_chorus8;
  }
  return NULL;
}

int main(int argc, char **argv) {
  const ifx_effect *effects[8];
  unsigned count = 0;
  if (argc == 1) {
    effects[count++] = &ifx_effect_delay;
    effects[count++] = &ifx_effect_chorus8;
  } else {
    for (int arg = 1; arg < argc; arg++) {
      const ifx_effect *effect = find_effect(argv[arg]);
      if (effect == NULL) {
        fprintf(stderr, "unknown effect: %s\n", argv[arg]);
        return 2;
      }
      effects[count++] = effect;
    }
  }
  int32_t *in = malloc(BENCH_SIGNAL_FRAMES * sizeof *in);
  if (in == NULL) {
    fprintf(stderr, "out of memory\n");
    return 1;
  }
  ifx_signal signal;
  ifx_signal_init(&signal);
  ifx_signal_fill(&signal, in, BENCH_SIGNAL_FRAMES);
  printf("%u Hz, %d-frame buffers, median of %d runs of %d frames\n", IFX_EFFECT_RATE, BENCH_BUFFER,
         BENCH_RUNS, BENCH_RUN_FRAMES);
  for (unsigned i = 0; i < count; i++) {
    const ifx_effect *effect = effects[i];
    int32_t values[32];
    ifx_effect_defaults(effect, values);
    const double ns = bench_measure(effect, values, in);
    if (ns < 0.0) {
      fprintf(stderr, "%s: cannot open\n", effect->name);
      free(in);
      return 1;
    }
    printf("%-17s %9.1f ns/frame   %6.2f %% of real time\n", effect->name, ns,
           ns * 100.0 / (1e9 / (double)IFX_EFFECT_RATE));
  }
  free(in);
  return 0;
}
