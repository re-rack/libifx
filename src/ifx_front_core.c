#include "ifx_front_core.h"

#include "ifx_dsp_math.h"

enum {
  LP_COEF = 0x7FDF3B, /* 0.99900 */
  IN_COEF = 0x0020C5, /* 0.0010001 */
};

void ifx_front_core_state_init(ifx_front_core_state *state) { state->lp = 0; }

void ifx_front_core_process(ifx_front_core_state *state, const int32_t in[IFX_BLOCK_FRAMES],
                            int32_t out[IFX_BLOCK_FRAMES], int32_t *level) {
  int64_t largest = 0;
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    const int32_t x = in[i];
    const int64_t sum = ifx_product(LP_COEF, state->lp) + ifx_product(IN_COEF, x);
    const int64_t b = ifx_round24(sum);
    state->lp = ifx_store24(b);
    const int64_t y = b - ifx_word_acc(x);
    out[i] = ifx_store24(y);
    const int64_t magnitude = y < 0 ? -y : y;
    if (magnitude > largest) {
      largest = magnitude;
    }
  }
  *level = ifx_store24(largest);
}
