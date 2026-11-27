#include "ifx_analog_core.h"

#include "ifx_analog_design.h"
#include "ifx_dsp_math.h"

enum { GAIN_SHIFT = 16, BLOCK_SHIFT = 5 };

_Static_assert(IFX_BLOCK_FRAMES == 1u << BLOCK_SHIFT, "the ramp divides by the block size");

static int32_t add_dry(int32_t out, int32_t in, int32_t latch) {
  const int64_t sum = out + ((-(int64_t)in * latch + 128) >> 8);
  if (sum > IFX_W24_MAX) {
    return IFX_W24_MAX;
  }
  return sum < IFX_W24_MIN ? IFX_W24_MIN : (int32_t)sum;
}

static int32_t scale(int32_t out, int32_t gain) {
  return (int32_t)(((int64_t)out * gain + (1 << (GAIN_SHIFT - 1))) >> GAIN_SHIFT);
}

void ifx_analog_core_state_init(ifx_analog_core_state *state, int32_t gain) { state->gain = gain; }

void ifx_analog_core_process(ifx_analog_core_state *state, const int32_t in[IFX_BLOCK_FRAMES],
                             int32_t latch_left, int32_t latch_right, int32_t gain,
                             int32_t out_left[IFX_BLOCK_FRAMES],
                             int32_t out_right[IFX_BLOCK_FRAMES]) {
  if (latch_left != 0 || latch_right != 0) {
    for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
      out_left[i] = add_dry(out_left[i], in[i], latch_left);
      out_right[i] = add_dry(out_right[i], in[i], latch_right);
    }
  }
  const int32_t from = state->gain;
  state->gain = gain;
  if (from == IFX_ANALOG_DESIGN_UNITY && gain == IFX_ANALOG_DESIGN_UNITY) {
    return;
  }
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    const int32_t step = from + (int32_t)(((int64_t)(gain - from) * (i + 1u)) >> BLOCK_SHIFT);
    out_left[i] = scale(out_left[i], step);
    out_right[i] = scale(out_right[i], step);
  }
}
