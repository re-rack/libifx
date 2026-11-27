#include "ifx_hush_core.h"

#include "ifx_dsp_math.h"

enum {
  UNITY = 0x7FFFFF,
  HUSH_OFF_GAIN = 0x7FFFF8,
  GAIN_TRIM = 0x100000, /* 0.125 */
  SMOOTH = 0x0CCCCD,    /* the 0.1 trim X:$000A the boot image leaves */
};

void ifx_hush_core_state_init(ifx_hush_core_state *state) { state->gain = 0; }

void ifx_hush_core_process(ifx_hush_core_state *state, const int32_t in[IFX_BLOCK_FRAMES],
                           int32_t out[IFX_BLOCK_FRAMES], int32_t level, int32_t threshold,
                           int32_t slope, int32_t release, int hush_on) {
  int64_t b = ifx_word_acc(UNITY);
  if (ifx_abs56(ifx_word_acc(threshold)) > ifx_abs56(ifx_word_acc(level))) {
    int64_t a = ifx_mpy(level, slope);
    a = ifx_asr_n(a, 8u);
    b = ifx_word_acc(ifx_move_a0(a));
  }

  const int32_t target = ifx_store24(b);
  int64_t a = ifx_mpyr(target, target);
  const int64_t released = ifx_mpyr(state->gain, release);
  a = ifx_sub(a, released);
  b = released;
  if (a > 0) {
    const int32_t slope_word = ifx_store24(a);
    a = ifx_mpy(slope_word, slope_word);
    b = ifx_add(b, a);
  }

  state->gain = ifx_store24(b);
  int64_t gain = ifx_macr(b, state->gain, SMOOTH);
  const int32_t g = ifx_store24(gain);
  gain = ifx_neg56(ifx_mpy(g, g));
  gain = ifx_add24(gain, g);
  gain = ifx_add24(gain, g);
  if (hush_on == 0) {
    gain = ifx_word_acc(HUSH_OFF_GAIN);
  }
  const int32_t shape = ifx_store24(gain);
  const int32_t multiplier = ifx_store24(ifx_mpyr(GAIN_TRIM, shape));
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    out[i] = ifx_store24(ifx_mpyr(multiplier, in[i]));
  }
}
