/* The Hush gain and block multiplier at P:$0417..$043B, shared by every
 * configuration. The threshold, slope and release words come from the shared
 * message handlers. */
#ifndef IFX_HUSH_CORE_H
#define IFX_HUSH_CORE_H

#include <stdint.h>

#include "ifx_base.h"

typedef struct {
  int32_t gain; /* X:$0008: the smoothed Hush gain, 0 at silence */
} ifx_hush_core_state;

void ifx_hush_core_state_init(ifx_hush_core_state *state);

/* One block: the level the DC blocker measured sets the target, the release
 * word smooths the gain, and the output carries the Hush block multiplier. */
void ifx_hush_core_process(ifx_hush_core_state *state, const int32_t in[IFX_BLOCK_FRAMES],
                           int32_t out[IFX_BLOCK_FRAMES], int32_t level, int32_t threshold,
                           int32_t slope, int32_t release, int hush_on);

#endif /* IFX_HUSH_CORE_H */
