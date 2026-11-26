/* The Ducker's smoothed attenuation, shared by configurations 0, 1 and 3:
 * towards the attenuation word while the block level reaches sensitivity,
 * towards unity with the release coefficient otherwise. */
#ifndef IFX_DUCKER_CORE_H
#define IFX_DUCKER_CORE_H

#include <stdint.h>

typedef struct {
  int32_t gain; /* Y:$0029: the current attenuation */
} ifx_ducker_core_state;

void ifx_ducker_core_state_init(ifx_ducker_core_state *state, int32_t release, int round_last);

/* `round_last` is configuration 0/1's MACR on the last term; configuration 3
 * uses MAC. */
void ifx_ducker_core_process(ifx_ducker_core_state *state, int32_t level, int32_t threshold,
                             int32_t attenuation, int32_t release, int round_last);

#endif /* IFX_DUCKER_CORE_H */
