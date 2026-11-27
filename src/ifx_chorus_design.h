/* The chorus voice message handlers' math: one voice block at Y:$0EC0 + 8*v
 * from LEVEL, PAN, DELAY, DEPTH and RATE. Configurations 0 and 4 share it. */
#ifndef IFX_CHORUS_DESIGN_H
#define IFX_CHORUS_DESIGN_H

#include <stdint.h>

/* Raw values: level 0..58, pan 0..100, delay 0..209 (2 ms steps), depth
 * 0..100, rate 0..254. */
void ifx_chorus_design_voice(int level, int pan, int delay, int depth, int rate, int32_t words[8]);

#endif /* IFX_CHORUS_DESIGN_H */
