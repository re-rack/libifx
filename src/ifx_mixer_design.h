/* The level, switch and pan words the message handlers stage: a level from T1
 * (the Mixer's at P:$053F..$054E), the X:$0000 Hush and DIRECT HUSH bits, and
 * the voice and delay handlers' level times T2 pan pairs. */
#ifndef IFX_MIXER_DESIGN_H
#define IFX_MIXER_DESIGN_H

#include <stdint.h>

enum {
  IFX_MIXER_DESIGN_FLAG_HUSH = 1 << 4,  /* X:$0000 bit 4: Hush in */
  IFX_MIXER_DESIGN_FLAG_DIRECT = 1 << 6 /* X:$0000 bit 6: DIRECT HUSH at POST */
};

/* Raw values: level 0..58, pan 0..100, the switches 0..1. */
int32_t ifx_mixer_design_level(int level);
int32_t ifx_mixer_design_flags(int hush, int direct_hush);
void ifx_mixer_design_pan(int32_t level, int pan, int32_t *left, int32_t *right);

#endif /* IFX_MIXER_DESIGN_H */
