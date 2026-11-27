#include "ifx_mixer_core.h"

#include "ifx_dsp_math.h"

static int32_t output(int64_t acc) {
  const int32_t word = ifx_store24(ifx_addl(ifx_word_acc(IFX_MIXER_CORE_OFFSET), acc));
  return ifx_store24(ifx_sub24(ifx_word_acc(word), IFX_MIXER_CORE_OFFSET));
}

void ifx_mixer_core_process(const int32_t wet_left[IFX_BLOCK_FRAMES],
                            const int32_t wet_right[IFX_BLOCK_FRAMES],
                            const int32_t hush[IFX_BLOCK_FRAMES], int32_t wet_level,
                            int32_t direct_left, int32_t direct_right, int direct_on,
                            int32_t out_left[IFX_BLOCK_FRAMES],
                            int32_t out_right[IFX_BLOCK_FRAMES]) {
  const int32_t dir_l = direct_on != 0 ? direct_left : 0;
  const int32_t dir_r = direct_on != 0 ? direct_right : 0;
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    int64_t a = ifx_mpy(wet_level, wet_left[i]);
    a = ifx_mac(a, dir_l, hush[i]);
    a = ifx_asl(ifx_asl(a));
    out_left[i] = output(a);

    a = ifx_mpy(hush[i], dir_r);
    a = ifx_mac(a, wet_level, wet_right[i]);
    a = ifx_asl(ifx_asl(a));
    out_right[i] = output(a);
  }
}
