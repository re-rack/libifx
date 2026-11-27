#include "ifx_chorus8_setup.h"

#include "ifx_analog_design.h"
#include "ifx_chorus_design.h"
#include "ifx_hush_design.h"
#include "ifx_mixer_design.h"

/* Exclusive maxima: the MCU's windows at EPROM 0x12C8, and the library's
 * ANALOG_DIRECT and OUTPUT_LEVEL. */
enum {
  CHORUS_LEVEL_MIN = 0,
  CHORUS_LEVEL_MAX = 59,
  DIRECT_LEFT_MIN = 0,
  DIRECT_LEFT_MAX = 59,
  DIRECT_RIGHT_MIN = 0,
  DIRECT_RIGHT_MAX = 59,
  DIRECT_HUSH_MIN = 0,
  DIRECT_HUSH_MAX = 2,
  ANALOG_DIRECT_MIN = 0,
  ANALOG_DIRECT_MAX = 2,
  OUTPUT_LEVEL_MIN = 0,
  OUTPUT_LEVEL_MAX = 101,
  REGEN_LEFT_MIN = 0,
  REGEN_LEFT_MAX = 59,
  REGEN_RIGHT_MIN = 0,
  REGEN_RIGHT_MAX = 59,
  HUSH_MIN = 0,
  HUSH_MAX = 2,
  HUSH_THRESHOLD_MIN = 0,
  HUSH_THRESHOLD_MAX = 25,
  HUSH_RELEASE_MIN = 0,
  HUSH_RELEASE_MAX = 10,
  LEVEL_MIN = 0,
  LEVEL_MAX = 59,
  PAN_MIN = 0,
  PAN_MAX = 101,
  DELAY_MIN = 0,
  DELAY_MAX = 210,
  DEPTH_MIN = 0,
  DEPTH_MAX = 101,
  RATE_MIN = 0,
  RATE_MAX = 255,
};

int ifx_chorus8_setup_config(const ifx_chorus8_setup_raw *raw, ifx_chorus8_config *config) {
  if (raw == NULL || config == NULL) {
    return -1;
  }

  const int chorus_level = ifx_window(raw->chorus_level, CHORUS_LEVEL_MIN, CHORUS_LEVEL_MAX);
  const int direct_left = ifx_window(raw->direct_left, DIRECT_LEFT_MIN, DIRECT_LEFT_MAX);
  const int direct_right = ifx_window(raw->direct_right, DIRECT_RIGHT_MIN, DIRECT_RIGHT_MAX);
  const int direct_hush = ifx_window(raw->direct_hush, DIRECT_HUSH_MIN, DIRECT_HUSH_MAX);
  const int analog_direct = ifx_window(raw->analog_direct, ANALOG_DIRECT_MIN, ANALOG_DIRECT_MAX);
  const int output_level = ifx_window(raw->output_level, OUTPUT_LEVEL_MIN, OUTPUT_LEVEL_MAX);
  const int regen_left = ifx_window(raw->regen_left, REGEN_LEFT_MIN, REGEN_LEFT_MAX);
  const int regen_right = ifx_window(raw->regen_right, REGEN_RIGHT_MIN, REGEN_RIGHT_MAX);
  const int hush = ifx_window(raw->hush, HUSH_MIN, HUSH_MAX);
  const int hush_threshold =
      ifx_window(raw->hush_threshold, HUSH_THRESHOLD_MIN, HUSH_THRESHOLD_MAX);
  const int hush_release = ifx_window(raw->hush_release, HUSH_RELEASE_MIN, HUSH_RELEASE_MAX);

  ifx_hush_design_words hush_words;
  ifx_hush_design_words_make(hush_threshold, hush_release, &hush_words);

  config->coef[IFX_CHORUS8_COEF_WET] = ifx_mixer_design_level(chorus_level);
  config->coef[IFX_CHORUS8_COEF_DIRECT_LEFT] = ifx_mixer_design_level(direct_left);
  config->coef[IFX_CHORUS8_COEF_DIRECT_RIGHT] = ifx_mixer_design_level(direct_right);
  config->coef[IFX_CHORUS8_COEF_FLAGS] = ifx_mixer_design_flags(hush, direct_hush);
  config->coef[IFX_CHORUS8_COEF_HUSH_THRESHOLD] = hush_words.threshold;
  config->coef[IFX_CHORUS8_COEF_HUSH_SLOPE] = hush_words.slope;
  config->coef[IFX_CHORUS8_COEF_HUSH_RELEASE] = hush_words.release;
  config->coef[IFX_CHORUS8_COEF_REG_LEFT] = ifx_mixer_design_level(regen_left);
  config->coef[IFX_CHORUS8_COEF_REG_RIGHT] = ifx_mixer_design_level(regen_right);

  const struct {
    int level;
    int pan;
    int delay;
    int depth;
    int rate;
  } voices[8] = {
      {raw->level1, raw->pan1, raw->delay1, raw->depth1, raw->rate1},
      {raw->level2, raw->pan2, raw->delay2, raw->depth2, raw->rate2},
      {raw->level3, raw->pan3, raw->delay3, raw->depth3, raw->rate3},
      {raw->level4, raw->pan4, raw->delay4, raw->depth4, raw->rate4},
      {raw->level5, raw->pan5, raw->delay5, raw->depth5, raw->rate5},
      {raw->level6, raw->pan6, raw->delay6, raw->depth6, raw->rate6},
      {raw->level7, raw->pan7, raw->delay7, raw->depth7, raw->rate7},
      {raw->level8, raw->pan8, raw->delay8, raw->depth8, raw->rate8},
  };
  for (unsigned voice = 0; voice < 8u; voice++) {
    const int level = ifx_window(voices[voice].level, LEVEL_MIN, LEVEL_MAX);
    const int pan = ifx_window(voices[voice].pan, PAN_MIN, PAN_MAX);
    const int delay = ifx_window(voices[voice].delay, DELAY_MIN, DELAY_MAX);
    const int depth = ifx_window(voices[voice].depth, DEPTH_MIN, DEPTH_MAX);
    const int rate = ifx_window(voices[voice].rate, RATE_MIN, RATE_MAX);
    ifx_chorus_design_voice(level, pan, delay, depth, rate,
                            &config->coef[IFX_CHORUS8_COEF_VOICES + 8u * voice]);
  }
  config->direct_latch[0] = ifx_analog_design_latch(analog_direct, direct_hush, direct_left);
  config->direct_latch[1] = ifx_analog_design_latch(analog_direct, direct_hush, direct_right);
  config->output_gain = ifx_analog_design_output(output_level);
  return 0;
}
