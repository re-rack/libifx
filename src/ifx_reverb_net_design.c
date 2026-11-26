#include "ifx_reverb_net_design.h"

#include "ifx_dsp_math.h"
#include "ifx_tables.h"

enum { DECAY_BASE = 0x4CCCCD, DECAY_STEP = 0x004189 };

void ifx_reverb_net_design_words_make(int decay, int damp, int pan, int mix,
                                      ifx_reverb_net_design_words *words) {
  const int32_t decay_word =
      ifx_s24((uint32_t)(DECAY_BASE + (uint32_t)ifx_a0(ifx_mpy(DECAY_STEP, decay))));
  words->decay = decay_word;
  words->damp = ifx_s24((uint32_t)ifx_a0(ifx_mpy(DECAY_STEP, damp)));
  words->feedback = ifx_store24(ifx_macr(ifx_word_acc(decay_word), -words->damp, decay_word));
  words->pan_left = ifx_pan_table[pan & 0x7F];
  words->pan_right = ifx_pan_table[100 - pan];
  words->mix_left = ifx_pan_table[mix & 0x7F];
  words->mix_right = ifx_pan_table[100 - mix];
}
