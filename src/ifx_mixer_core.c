#include "ifx_mixer_core.h"

#include "ifx_dsp_math.h"

void ifx_mixer_core_process(const int32_t wet_left[32], const int32_t wet_right[32],
                            const int32_t hush[32], int32_t wet_level, int32_t direct_left,
                            int32_t direct_right, int direct_on, int32_t out_left[32],
                            int32_t out_right[32]) {
  const int64_t offset = ifx_word_acc(IFX_MIXER_OFFSET);
  const int32_t dir_l = direct_on != 0 ? direct_left : 0;
  const int32_t dir_r = direct_on != 0 ? direct_right : 0;
  for (unsigned i = 0; i < 32u; i++) {
    int64_t a = ifx_mpy(wet_level, wet_left[i]);
    a = ifx_mac(a, dir_l, hush[i]);
    a = ifx_asl(a);
    a = ifx_asl(a);
    a = ifx_addl(offset, a);
    out_left[i] = ifx_store24(a);

    a = ifx_mpy(hush[i], dir_r);
    a = ifx_mac(a, wet_level, wet_right[i]);
    a = ifx_asl(a);
    a = ifx_asl(a);
    a = ifx_addl(offset, a);
    out_right[i] = ifx_store24(a);
  }
}
