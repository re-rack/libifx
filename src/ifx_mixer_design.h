/* The Mixer message handlers' words, P:$053F..$054E: the T1 level of the
 * effect, left and right direct levels, and the DIRECT HUSH bit. */
#ifndef IFX_MIXER_DESIGN_H
#define IFX_MIXER_DESIGN_H

#include <stdint.h>

enum {
  IFX_MIXER_FLAG_HUSH = 1u << 4,  /* X:$0000 bit 4: Hush in */
  IFX_MIXER_FLAG_DIRECT = 1u << 6 /* X:$0000 bit 6: DIRECT HUSH at POST */
};

int32_t ifx_mixer_design_level(int level);

#endif /* IFX_MIXER_DESIGN_H */
