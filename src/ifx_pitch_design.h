/* The pitch voice message handlers' math: one voice block at Y:$0EC0 + 8*v
 * from PITCH, FINE, LEVEL, PAN and DELAY. Configurations 5 and 1 share it. */
#ifndef IFX_PITCH_DESIGN_H
#define IFX_PITCH_DESIGN_H

#include <stdint.h>

/* Raw values: pitch 0..180, fine 0..40, level 0..58, pan 0..100, delay 0..209
 * (2 ms steps). */
void ifx_pitch_design_voice(int pitch, int fine, int level, int pan, int delay, int32_t words[8]);

#endif /* IFX_PITCH_DESIGN_H */
