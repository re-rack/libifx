#include "ifx_ducker_core.h"

#include "ifx_dsp_math.h"

enum {
  UNITY = 0x7FFFFF,
  DUCK_COEF = 0x76B296,
};

void ifx_ducker_core_state_init(ifx_ducker_core_state *state) { state->gain = UNITY; }

void ifx_ducker_core_process(ifx_ducker_core_state *state, int32_t level, int32_t threshold,
                             int32_t attenuation, int32_t release) {
  int32_t target = UNITY;
  int32_t coef = release;
  if (ifx_abs56(ifx_word_acc(level)) >= ifx_abs56(ifx_word_acc(threshold))) {
    target = attenuation;
    coef = DUCK_COEF;
  }
  int64_t a = ifx_word_acc(target);
  a = ifx_mac(a, -target, coef);
  a = ifx_mac(a, coef, state->gain);
  state->gain = ifx_store24(a);
}
