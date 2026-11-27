/* The shared memory wipe at P:$0079, which configurations 2 and 3 restart on a
 * mode change: the arriving pass skips the configuration, each later pass
 * clears the configuration's output block and 2,048 words of delay memory from
 * $FFFF down to $1000, one pass then still skips, and the next initialises the
 * configuration and runs it.
 */
#ifndef IFX_WIPE_CORE_H
#define IFX_WIPE_CORE_H

#include <stdint.h>

#include "ifx_base.h"

/* Passes from the one a restart arrives in to the one that initialises. */
#define IFX_WIPE_CORE_BLOCKS 32u

typedef struct {
  uint32_t pointer; /* X:$0017: the next word to clear */
  int active;       /* X:$0000 bit 11: the configuration is not called */
  int init_pending; /* X:$0000 bit 3: the coming call initialises */
  int skip_pass;    /* X:$0000 bit 12: one call still skips the configuration */
} ifx_wipe_core_state;

typedef enum {
  IFX_WIPE_CORE_SKIP, /* the configuration does not run this pass */
  IFX_WIPE_CORE_INIT, /* it initialises, then runs */
  IFX_WIPE_CORE_RUN,
} ifx_wipe_core_step;

void ifx_wipe_core_state_init(ifx_wipe_core_state *state);

/* A mode message: restarts the wipe unless an initialisation is pending. */
void ifx_wipe_core_restart(ifx_wipe_core_state *state);

/* The start of a pass: while the wipe runs, clear the output block and the
 * next step of memory. */
void ifx_wipe_core_pass(ifx_wipe_core_state *state, int32_t memory[0x10000],
                        int32_t wet_left[IFX_BLOCK_FRAMES], int32_t wet_right[IFX_BLOCK_FRAMES]);

/* What the configuration does this pass. */
ifx_wipe_core_step ifx_wipe_core_next(ifx_wipe_core_state *state);

#endif /* IFX_WIPE_CORE_H */
