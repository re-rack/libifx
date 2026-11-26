#include "ifx_pitch_design.h"

#include "ifx_dsp_math.h"
#include "ifx_mixer_design.h"
#include "ifx_tables.h"

enum { DELAY_SCALE = 78 };

void ifx_pitch_design_voice(int pitch, int fine, int level, int pan, int delay, int32_t words[8]) {
  const int32_t level_word = ifx_mixer_design_level(level);
  const int32_t pan_word = pan & 0x7F;
  words[0] = ifx_coarse_pitch_table[pitch & 0xFF];
  words[1] = ifx_fine_pitch_table[fine & 0x3F];
  words[2] = ifx_store24(ifx_mpyr(words[0], words[1]));
  words[3] = DELAY_SCALE * (delay & 0xFF);
  words[4] = level_word;
  words[5] = pan_word;
  words[6] = ifx_store24(ifx_mpyr(level_word, ifx_pan_table[pan_word]));
  words[7] = ifx_store24(ifx_mpyr(level_word, ifx_pan_table[100 - pan_word]));
}
