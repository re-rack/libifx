/* The analog stages after the D/A, which the firmware does not have: models,
 * not ports. The direct path scales the input as an 8-bit multiplying
 * attenuator, latch/256, negated to match Direct Post's polarity, and the sum
 * saturates to Q23. OUTPUT LEVEL then scales both sides. A change of either
 * latch or the gain moves all three along one eased ramp (ifx_fade.h), from
 * where they are, so turns and switches do not click. */
#ifndef IFX_ANALOG_CORE_H
#define IFX_ANALOG_CORE_H

#include <stdint.h>

#include "ifx_base.h"

enum { IFX_ANALOG_CORE_LEFT, IFX_ANALOG_CORE_RIGHT, IFX_ANALOG_CORE_GAIN, IFX_ANALOG_CORE_WORDS };

typedef struct {
  int32_t level[IFX_ANALOG_CORE_WORDS]; /* the latches in 2^-16 units and the gain, now */
  int32_t from[IFX_ANALOG_CORE_WORDS];  /* where the ramp started */
  int32_t to[IFX_ANALOG_CORE_WORDS];    /* where it ends */
  uint32_t pos;                         /* frames into the ramp, IFX_FADE_FRAMES at rest */
} ifx_analog_core_state;

void ifx_analog_core_state_init(ifx_analog_core_state *state, int32_t latch_left,
                                int32_t latch_right, int32_t gain);

void ifx_analog_core_process(ifx_analog_core_state *state, const int32_t in[IFX_BLOCK_FRAMES],
                             int32_t latch_left, int32_t latch_right, int32_t gain,
                             int32_t out_left[IFX_BLOCK_FRAMES],
                             int32_t out_right[IFX_BLOCK_FRAMES]);

#endif /* IFX_ANALOG_CORE_H */
