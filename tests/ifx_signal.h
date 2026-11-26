/* The mono test pattern every fixture audio run is driven with: 4,096 frames
 * that repeat, with the noise continuing from one repeat to the next.
 *
 *   0-1023    full-scale noise
 *   1024-2047 sawtooth 220 Hz, -6 dBFS
 *   2048-2559 noise bursts at -6 dBFS, 64 frames on and 64 off
 *   2560-3071 noise at -60 dBFS
 *   3072-3583 sweep from 20 Hz to just under Nyquist, -6 dBFS
 *   3584-4095 0x7FFFFF, -0x800000, then silence
 *
 * A four-cycle pitch transition window overrides the pattern: cycle 1 is a
 * clean monophonic note of 110 Hz that changes to 300 Hz at frame 2264, and
 * cycle 2 continues 300 Hz for its first 1024 frames. Cycle 3 is silent for
 * its first 2048 frames, following cycle 2's impulse into a long quiet
 * stretch. Cycle 0 runs the pattern above.
 *
 * Integer only, so x86-64 and AArch64 generate the same samples.
 */
#ifndef IFX_SIGNAL_H
#define IFX_SIGNAL_H

#include <stddef.h>
#include <stdint.h>

/* One step of the xorshift generator behind the noise and the recorder's random
 * operands. */
static inline uint32_t ifx_signal_xorshift(uint32_t *state) {
  uint32_t x = *state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state = x;
  return x;
}

typedef struct {
  uint32_t noise;
  uint32_t saw;
  uint32_t step;
  uint32_t sweep;
  uint32_t frame;
} ifx_signal;

void ifx_signal_init(ifx_signal *signal);

/* One frame of a signed 24-bit sample. */
void ifx_signal_next(ifx_signal *signal, int32_t *sample);

void ifx_signal_fill(ifx_signal *signal, int32_t *samples, size_t frames);

#endif /* IFX_SIGNAL_H */
