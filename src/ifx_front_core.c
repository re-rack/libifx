#include "ifx_front_core.h"

#include "ifx_dsp_math.h"

enum {
  LP_COEF = 0x7FDF3B, /* 0.99900 */
  IN_COEF = 0x0020C5, /* 0.0010001 */
};

void ifx_front_core_state_init(ifx_front_core_state *state) { state->lp = 0; }

void ifx_front_core_process(ifx_front_core_state *state, const int32_t in[32], int32_t out[32],
                            int32_t *level) {
  int64_t largest = 0;
  for (unsigned i = 0; i < 32u; i++) {
    const int32_t x = in[i];
    int64_t b = ifx_mpy(LP_COEF, state->lp);
    b = ifx_macr(b, IN_COEF, x);
    state->lp = ifx_store24(b);
    b = ifx_sub24(b, x);
    out[i] = ifx_store24(b);
    if (ifx_abs56(b) > ifx_abs56(largest)) {
      largest = b;
    }
  }
  *level = ifx_store24(ifx_abs56(largest));
}
