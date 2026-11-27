/* The names every module shares: the block size, the device's rate and the
 * knob windows. */
#ifndef IFX_BASE_H
#define IFX_BASE_H

/* The firmware processes 32-frame blocks, so the port has one block of
 * latency. */
#define IFX_BLOCK_FRAMES 32u

/* The device's own rate in Hz, which a host resamples to: the firmware has no
 * rate math, and it turns a millisecond into 39 samples. */
#define IFX_DEVICE_RATE 39000.0

/* A raw knob forced into the MCU's window: `minimum` up to the exclusive
 * `maximum` the firmware's window table holds. */
static inline int ifx_window(int value, int minimum, int maximum) {
  if (value < minimum) {
    return minimum;
  }
  return value >= maximum ? maximum - 1 : value;
}

#endif /* IFX_BASE_H */
