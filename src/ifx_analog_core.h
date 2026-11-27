/* The analog stages after the D/A, which the firmware does not have: models,
 * not ports. The direct path scales the input as an 8-bit multiplying
 * attenuator, latch/256, negated to match Direct Post's polarity, and the sum
 * saturates to Q23. OUTPUT LEVEL then scales both sides, ramping linearly over
 * a block from the last block's gain so turns do not click. */
#ifndef IFX_ANALOG_CORE_H
#define IFX_ANALOG_CORE_H

#include <stdint.h>

#include "ifx_base.h"

typedef struct {
  int32_t gain; /* OUTPUT LEVEL at the end of the last block */
} ifx_analog_core_state;

void ifx_analog_core_state_init(ifx_analog_core_state *state, int32_t gain);

void ifx_analog_core_process(ifx_analog_core_state *state, const int32_t in[IFX_BLOCK_FRAMES],
                             int32_t latch_left, int32_t latch_right, int32_t gain,
                             int32_t out_left[IFX_BLOCK_FRAMES],
                             int32_t out_right[IFX_BLOCK_FRAMES]);

#endif /* IFX_ANALOG_CORE_H */
