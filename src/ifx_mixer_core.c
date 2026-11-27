#include "ifx_mixer_core.h"

#include "ifx_dsp_math.h"

enum { OUT_MAX = IFX_W24_MAX - IFX_MIXER_CORE_OFFSET };

/* The two ASLs and ADDL's doubling put the products' sum over 2^21 into A1.
 * The limiter acts with the offset added, so the top word is $7FFFFF less it. */
static int32_t output(int64_t sum) {
  const int32_t word = (int32_t)(sum >> 21);
  return word > OUT_MAX ? OUT_MAX : (word < IFX_W24_MIN ? IFX_W24_MIN : word);
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
    out_left[i] = output(ifx_product(wet_level, wet_left[i]) + ifx_product(dir_l, hush[i]));
    out_right[i] = output(ifx_product(hush[i], dir_r) + ifx_product(wet_level, wet_right[i]));
  }
}
