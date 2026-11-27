#include "ifx_reverb_net_design.h"

#include "ifx_dsp_math.h"
#include "ifx_tables.h"

enum { DECAY_BASE = 0x4CCCCD, DECAY_STEP = 0x004189 };

void ifx_reverb_net_design_words_make(int decay, int damp, int pan,
                                      ifx_reverb_net_design_words *words) {
  words->decay = ifx_s24((uint32_t)(DECAY_BASE + ifx_a0(ifx_mpy(DECAY_STEP, decay & 0x7F))));
  words->damp = ifx_move_a0(ifx_mpy(DECAY_STEP, damp & 0x7F));
  words->feedback = ifx_store24(ifx_macr(ifx_word_acc(words->decay), -words->damp, words->decay));
  words->pan_left = ifx_pan_table[pan & 0x7F];
  words->pan_right = ifx_pan_table[(100 - pan) & 0x7F];
}

void ifx_reverb_net_design_mix(int mix, int32_t *left, int32_t *right) {
  *left = ifx_pan_table[mix & 0x7F];
  *right = ifx_pan_table[(100 - mix) & 0x7F];
}
