/* The block front every configuration shares: the DC blocker at P:$0040 and
 * its block level, the largest magnitude of the high-passed input. */
#ifndef IFX_FRONT_CORE_H
#define IFX_FRONT_CORE_H

#include <stdint.h>

typedef struct {
  int32_t lp; /* X:$000D: the blocker's low-pass state */
} ifx_front_core_state;

void ifx_front_core_state_init(ifx_front_core_state *state);

/* 32 input words in, the high-passed words and their largest magnitude out. */
void ifx_front_core_process(ifx_front_core_state *state, const int32_t in[32], int32_t out[32],
                            int32_t *level);

#endif /* IFX_FRONT_CORE_H */
