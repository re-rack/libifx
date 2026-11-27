#include "ifx_chorus_design.h"

#include "ifx_dsp_math.h"
#include "ifx_mixer_design.h"

enum {
  PITCH_SCALE = 0x0020C5,
  DELAY_SCALE = 78,
};

void ifx_chorus_design_voice(int level, int pan, int delay, int depth, int rate, int32_t words[8]) {
  const int32_t level_word = ifx_mixer_design_level(level);
  words[0] = ifx_move_a0(ifx_mpy(rate & 0xFF, PITCH_SCALE));
  words[1] = ifx_move_a0(ifx_mpy(depth & 0x7F, PITCH_SCALE));
  words[2] = 0;
  words[3] = DELAY_SCALE * (delay & 0xFF);
  words[4] = level_word;
  words[5] = pan & 0x7F;
  ifx_mixer_design_pan(level_word, pan, &words[6], &words[7]);
}
