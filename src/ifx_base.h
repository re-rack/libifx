/* The names every module shares: the block size, the host's sample-rate
 * context and the knob windows. */
#ifndef IFX_BASE_H
#define IFX_BASE_H

#include <math.h>

/* The firmware processes 32-frame blocks, so the port has one block of
 * latency. */
#define IFX_BLOCK_FRAMES 32u

/* The device's own rate, in Hz: the firmware turns a millisecond into 39
 * samples. */
#define IFX_DEVICE_RATE 39000.0

typedef struct {
  double rate; /* Hz; 0 = no rate */
} ifx_context;

/* Sets the rate in Hz: 0 on success; -1 for a NULL context or a rate that is
 * not finite and greater than zero. A rejected rate leaves the context without
 * one. */
int ifx_context_init(ifx_context *context, double rate);

/* The rate a setup may read, or 0 when the context holds no valid one. */
static inline double ifx_context_rate(const ifx_context *context) {
  if (!context || !isfinite(context->rate) || context->rate <= 0.0) {
    return 0.0;
  }
  return context->rate;
}

/* A raw knob forced into the MCU's window: `minimum` up to the exclusive
 * `maximum` the firmware's window table holds. */
static inline int ifx_window(int value, int minimum, int maximum) {
  if (value < minimum) {
    return minimum;
  }
  return value >= maximum ? maximum - 1 : value;
}

#endif /* IFX_BASE_H */
