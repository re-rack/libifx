#include "ifx_signal.h"

#define PATTERN_FRAMES 4096u
#define PART_FRAMES 512u
#define SWEEP_FIRST 3072u
#define SWEEP_FRAMES 512u
#define RATE 39000u
#define HALF_SCALE 0x400000

/* One cycle of `hz` as a phase step over the 2^32 range. */
static uint32_t hz_step(unsigned hz) { return (uint32_t)(((uint64_t)hz << 32) / RATE); }

/* Uniform over the whole 24-bit range. */
static int32_t noise_sample(uint32_t *state) {
  return (int32_t)(ifx_signal_xorshift(state) >> 8) - 0x800000;
}

static int32_t saw_sample(uint32_t *phase, uint32_t step) {
  int32_t value = (int32_t)(*phase >> 8) - 0x800000;
  *phase += step;
  return value / 2;
}

/* A parabola through the half cycles, within a fraction of a percent of a sine
 * and free of both libm and the host's rounding mode. */
static int32_t sine_sample(uint32_t phase) {
  uint32_t word = phase >> 8;
  int32_t t = (word & 0x800000u) != 0u ? (int32_t)(0xFFFFFFu - word) : (int32_t)word;
  int32_t bump = (int32_t)(((int64_t)t * (int64_t)(0x1000000 - t)) >> 23);
  int32_t value = (int32_t)(((int64_t)bump * HALF_SCALE) >> 23);
  return (phase & 0x80000000u) != 0u ? -value : value;
}

void ifx_signal_init(ifx_signal *signal) {
  signal->noise = 0x2545F491u;
  signal->saw = 0;
  signal->step = hz_step(220);
  signal->sweep = 0;
  signal->frame = 0;
}

void ifx_signal_next(ifx_signal *signal, int32_t *sample) {
  uint32_t cycle = signal->frame / PATTERN_FRAMES;
  uint32_t part = signal->frame & (PATTERN_FRAMES - 1u);
  signal->frame++;
  if ((cycle & 3u) == 1u) {
    unsigned hz = part < 2264u ? 110u : 300u;
    *sample = sine_sample(signal->sweep);
    signal->sweep += hz_step(hz);
    return;
  }
  if ((cycle & 3u) == 2u && part < 1024u) {
    *sample = sine_sample(signal->sweep);
    signal->sweep += hz_step(300u);
    return;
  }
  if ((cycle & 3u) == 3u && part < 2048u) {
    *sample = 0;
    return;
  }
  if (part < 1024u) {
    *sample = noise_sample(&signal->noise);
  } else if (part < 2048u) {
    *sample = saw_sample(&signal->saw, signal->step);
  } else if (part < 2048u + PART_FRAMES) {
    uint32_t n = part - 2048u;
    *sample = (n % 128u) < 64u ? noise_sample(&signal->noise) / 2 : 0;
  } else if (part < 3072u) {
    *sample = noise_sample(&signal->noise) / 1024;
  } else if (part < 3584u) {
    unsigned index = part - SWEEP_FIRST;
    if (index == 0u) {
      signal->sweep = 0;
    }
    *sample = sine_sample(signal->sweep);
    signal->sweep += hz_step(20u + ((RATE / 2u - 1u - 20u) * index) / (SWEEP_FRAMES - 1u));
  } else {
    uint32_t n = part - 3584u;
    *sample = n == 0u ? 0x7FFFFF : n == 1u ? -0x800000 : 0;
  }
}

void ifx_signal_fill(ifx_signal *signal, int32_t *samples, size_t frames) {
  for (size_t i = 0; i < frames; i++) {
    ifx_signal_next(signal, samples + i);
  }
}
