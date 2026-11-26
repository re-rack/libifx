#include "ifx_regen_core.h"

#include "ifx_dsp_math.h"

enum { LIMIT_COEF = 0x71EB85 }; /* 0.8889 */

void ifx_regen_core_limit(int32_t *left, int32_t *right) {
  int64_t sum = ifx_word_acc(*left);
  sum = ifx_add24(sum, *right);
  if (!ifx_es(sum)) {
    return;
  }
  const int64_t scaled_left = ifx_mpyr(*left, LIMIT_COEF);
  const int64_t scaled_right = ifx_mpyr(*right, LIMIT_COEF);
  *left = ifx_store24(scaled_left);
  *right = ifx_store24(scaled_right);
}
