#include "ifx_ducker_core.h"

#include "ifx_dsp_math.h"

enum {
  UNITY = 0x7FFFFF,
  DUCK_COEF = 0x76B296,
};

static int32_t step(int32_t gain, int32_t target, int32_t coef, int round_last) {
  int64_t a = ifx_word_acc(target);
  a = ifx_mac(a, -target, coef);
  a = round_last != 0 ? ifx_macr(a, coef, gain) : ifx_mac(a, coef, gain);
  return ifx_store24(a);
}

/* No configuration initialises Y:$0029. A release from below stops at the
 * lowest word the rounding holds still, so that is the gain at rest. */
void ifx_ducker_core_state_init(ifx_ducker_core_state *state, int32_t release, int round_last) {
  int32_t gain = UNITY;
  while (step(gain - 1, UNITY, release, round_last) == gain - 1) {
    gain--;
  }
  state->gain = gain;
}

void ifx_ducker_core_process(ifx_ducker_core_state *state, int32_t level, int32_t threshold,
                             int32_t attenuation, int32_t release, int round_last) {
  if (ifx_abs56(ifx_word_acc(level)) >= ifx_abs56(ifx_word_acc(threshold))) {
    state->gain = step(state->gain, attenuation, DUCK_COEF, round_last);
  } else {
    state->gain = step(state->gain, UNITY, release, round_last);
  }
}
