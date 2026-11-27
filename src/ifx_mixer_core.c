#include "ifx_mixer_core.h"

#include "ifx_dsp_math.h"

enum { OUT_MAX = IFX_W24_MAX - IFX_MIXER_CORE_OFFSET };

/* The two ASLs scale the word sum by 8: the limiter's 2^23 grid is 2^20 on the
 * raw sum, and the analog stage's offset leaves $799999 as the top word. */
static int32_t output(int64_t sum) {
  const int32_t word = (int32_t)(sum >> 20);
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
    out_left[i] = output((int64_t)wet_level * wet_left[i] + (int64_t)dir_l * hush[i]);
    out_right[i] = output((int64_t)hush[i] * dir_r + (int64_t)wet_level * wet_right[i]);
  }
}
