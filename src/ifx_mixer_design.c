#include "ifx_mixer_design.h"

#include "ifx_dsp_math.h"
#include "ifx_tables.h"

int32_t ifx_mixer_design_level(int level) { return ifx_level_table[level & 0x3F]; }

int32_t ifx_mixer_design_flags(int hush, int direct_hush) {
  return (hush != 0 ? IFX_MIXER_DESIGN_FLAG_HUSH : 0) |
         (direct_hush != 0 ? IFX_MIXER_DESIGN_FLAG_DIRECT : 0);
}

void ifx_mixer_design_pan(int32_t level, int pan, int32_t *left, int32_t *right) {
  *left = ifx_store24(ifx_mpyr(level, ifx_pan_table[pan & 0x7F]));
  *right = ifx_store24(ifx_mpyr(level, ifx_pan_table[(100 - pan) & 0x7F]));
}
