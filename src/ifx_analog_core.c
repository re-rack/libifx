#include "ifx_analog_core.h"

#include "ifx_analog_design.h"
#include "ifx_dsp_math.h"
#include "ifx_fade.h"

enum { LATCH_SHIFT = 16, GAIN_SHIFT = 16 };

static int32_t add_dry(int32_t out, int32_t in, int32_t latch) {
  const int64_t sum =
      out + ((-(int64_t)in * latch + (1 << (LATCH_SHIFT + 7))) >> (LATCH_SHIFT + 8));
  if (sum > IFX_W24_MAX) {
    return IFX_W24_MAX;
  }
  return sum < IFX_W24_MIN ? IFX_W24_MIN : (int32_t)sum;
}

static int32_t scale(int32_t out, int32_t gain) {
  return (int32_t)(((int64_t)out * gain + (1 << (GAIN_SHIFT - 1))) >> GAIN_SHIFT);
}

void ifx_analog_core_state_init(ifx_analog_core_state *state, int32_t latch_left,
                                int32_t latch_right, int32_t gain) {
  const int32_t level[IFX_ANALOG_CORE_WORDS] = {latch_left * (1 << LATCH_SHIFT),
                                                latch_right * (1 << LATCH_SHIFT), gain};
  for (unsigned word = 0; word < IFX_ANALOG_CORE_WORDS; word++) {
    state->level[word] = level[word];
    state->from[word] = level[word];
    state->to[word] = level[word];
  }
  state->pos = IFX_FADE_FRAMES;
}

void ifx_analog_core_process(ifx_analog_core_state *state, const int32_t in[IFX_BLOCK_FRAMES],
                             int32_t latch_left, int32_t latch_right, int32_t gain,
                             int32_t out_left[IFX_BLOCK_FRAMES],
                             int32_t out_right[IFX_BLOCK_FRAMES]) {
  const int32_t target[IFX_ANALOG_CORE_WORDS] = {latch_left * (1 << LATCH_SHIFT),
                                                 latch_right * (1 << LATCH_SHIFT), gain};
  int32_t *const level = state->level;
  if (target[0] != state->to[0] || target[1] != state->to[1] || target[2] != state->to[2]) {
    for (unsigned word = 0; word < IFX_ANALOG_CORE_WORDS; word++) {
      state->from[word] = level[word];
      state->to[word] = target[word];
    }
    state->pos = 0;
  }
  if (state->pos == IFX_FADE_FRAMES && level[IFX_ANALOG_CORE_LEFT] == 0 &&
      level[IFX_ANALOG_CORE_RIGHT] == 0 && level[IFX_ANALOG_CORE_GAIN] == IFX_ANALOG_DESIGN_UNITY) {
    return;
  }
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    if (state->pos < IFX_FADE_FRAMES) {
      const int64_t share = ifx_fade_share(++state->pos);
      for (unsigned word = 0; word < IFX_ANALOG_CORE_WORDS; word++) {
        level[word] = ifx_fade_blend(state->from[word], state->to[word], share);
      }
    }
    out_left[i] = scale(add_dry(out_left[i], in[i], level[IFX_ANALOG_CORE_LEFT]),
                        level[IFX_ANALOG_CORE_GAIN]);
    out_right[i] = scale(add_dry(out_right[i], in[i], level[IFX_ANALOG_CORE_RIGHT]),
                         level[IFX_ANALOG_CORE_GAIN]);
  }
}
