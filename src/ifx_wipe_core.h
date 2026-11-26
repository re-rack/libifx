/* The shared memory wipe at P:$0079: a restart clears the configuration's
 * output block and then walks the delay memory from $FFFF down to $1000 in
 * 2048-word steps, one step per pass, before the configuration initialises
 * again. */
#ifndef IFX_WIPE_CORE_H
#define IFX_WIPE_CORE_H

#include <stdint.h>

typedef struct {
  uint32_t pointer; /* X:$0017: the next word to clear */
  int active;       /* X:$0000 bit 11: the configuration is not called */
  int init_pending; /* X:$0000 bit 3: the coming call initialises */
  int skip_pass;    /* X:$0000 bit 12: one call still skips the configuration */
} ifx_wipe_core;

void ifx_wipe_core_init(ifx_wipe_core *wipe);

/* The restart a mode message asks for: $FFFF, bits 7, 11 and 3. */
void ifx_wipe_core_restart(ifx_wipe_core *wipe);

/* One pass: clear the output block and the step the pointer names, or end the
 * wipe and let the next call initialise. */
void ifx_wipe_core_call(ifx_wipe_core *wipe, int32_t memory[0x10000], int32_t wet_left[32],
                        int32_t wet_right[32]);

#endif /* IFX_WIPE_CORE_H */
