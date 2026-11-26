#include "ifx_chorus_design.h"

#include "ifx_dsp_math.h"
#include "ifx_mixer_design.h"
#include "ifx_tables.h"

enum {
  PITCH_SCALE = 0x0020C5,
  DELAY_SCALE = 78,
};

void ifx_chorus_design_voice(int level, int pan, int delay, int depth, int rate, int32_t words[8]) {
  const int32_t level_word = ifx_mixer_design_level(level);
  const int32_t depth_word = ifx_a0(ifx_mpy(depth & 0x7F, PITCH_SCALE));
  const int32_t rate_word = ifx_a0(ifx_mpy(rate & 0xFF, PITCH_SCALE));
  const int32_t pan_word = pan & 0x7F;
  words[0] = rate_word;
  words[1] = depth_word;
  words[2] = 0;
  words[3] = DELAY_SCALE * (delay & 0xFF);
  words[4] = level_word;
  words[5] = pan_word;
  words[6] = ifx_store24(ifx_mpyr(level_word, ifx_pan_table[pan_word]));
  words[7] = ifx_store24(ifx_mpyr(level_word, ifx_pan_table[100 - pan_word]));
}
