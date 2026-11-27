#include "ifx_pitch_setup.h"

#include "ifx_hush_design.h"
#include "ifx_mixer_design.h"
#include "ifx_pitch_design.h"

/* The MCU's windows at EPROM 0x12C8, exclusive maxima. */
enum {
  PITCH_LEVEL_MIN = 0,
  PITCH_LEVEL_MAX = 59,
  DIRECT_LEFT_MIN = 0,
  DIRECT_LEFT_MAX = 59,
  DIRECT_RIGHT_MIN = 0,
  DIRECT_RIGHT_MAX = 59,
  DIRECT_HUSH_MIN = 0,
  DIRECT_HUSH_MAX = 2,
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
  PITCH_MIN = 0,
  PITCH_MAX = 181,
  FINE_MIN = 0,
  FINE_MAX = 41,
  LEVEL_MIN = 0,
  LEVEL_MAX = 59,
  PAN_MIN = 0,
  PAN_MAX = 101,
  DELAY_MIN = 0,
  DELAY_MAX = 210,
};

int ifx_pitch_setup_config(const ifx_context *context, const ifx_pitch_setup_raw *raw,
                           ifx_pitch_config *config) {
  if (raw == NULL || config == NULL || ifx_context_rate(context) == 0.0) {
    return -1;
  }

  const int pitch_level = ifx_window(raw->pitch_level, PITCH_LEVEL_MIN, PITCH_LEVEL_MAX);
  const int direct_left = ifx_window(raw->direct_left, DIRECT_LEFT_MIN, DIRECT_LEFT_MAX);
  const int direct_right = ifx_window(raw->direct_right, DIRECT_RIGHT_MIN, DIRECT_RIGHT_MAX);
  const int direct_hush = ifx_window(raw->direct_hush, DIRECT_HUSH_MIN, DIRECT_HUSH_MAX);
  const int regen_left = ifx_window(raw->regen_left, REGEN_LEFT_MIN, REGEN_LEFT_MAX);
  const int regen_right = ifx_window(raw->regen_right, REGEN_RIGHT_MIN, REGEN_RIGHT_MAX);
  const int hush = ifx_window(raw->hush, HUSH_MIN, HUSH_MAX);
  const int hush_threshold =
      ifx_window(raw->hush_threshold, HUSH_THRESHOLD_MIN, HUSH_THRESHOLD_MAX);
  const int hush_release = ifx_window(raw->hush_release, HUSH_RELEASE_MIN, HUSH_RELEASE_MAX);

  ifx_hush_design_words hush_words;
  ifx_hush_design_words_make(hush_threshold, hush_release, &hush_words);

  config->coef[IFX_PITCH_COEF_WET] = ifx_mixer_design_level(pitch_level);
  config->coef[IFX_PITCH_COEF_DIRECT_LEFT] = ifx_mixer_design_level(direct_left);
  config->coef[IFX_PITCH_COEF_DIRECT_RIGHT] = ifx_mixer_design_level(direct_right);
  config->coef[IFX_PITCH_COEF_FLAGS] = ifx_mixer_design_flags(hush, direct_hush);
  config->coef[IFX_PITCH_COEF_HUSH_THRESHOLD] = hush_words.threshold;
  config->coef[IFX_PITCH_COEF_HUSH_SLOPE] = hush_words.slope;
  config->coef[IFX_PITCH_COEF_HUSH_RELEASE] = hush_words.release;
  config->coef[IFX_PITCH_COEF_REG_LEFT] = ifx_mixer_design_level(regen_left);
  config->coef[IFX_PITCH_COEF_REG_RIGHT] = ifx_mixer_design_level(regen_right);

  const struct {
    int pitch;
    int fine;
    int level;
    int pan;
    int delay;
  } voices[4] = {
      {raw->pitch1, raw->fine1, raw->level1, raw->pan1, raw->delay1},
      {raw->pitch2, raw->fine2, raw->level2, raw->pan2, raw->delay2},
      {raw->pitch3, raw->fine3, raw->level3, raw->pan3, raw->delay3},
      {raw->pitch4, raw->fine4, raw->level4, raw->pan4, raw->delay4},
  };
  for (unsigned voice = 0; voice < 4u; voice++) {
    const int pitch = ifx_window(voices[voice].pitch, PITCH_MIN, PITCH_MAX);
    const int fine = ifx_window(voices[voice].fine, FINE_MIN, FINE_MAX);
    const int level = ifx_window(voices[voice].level, LEVEL_MIN, LEVEL_MAX);
    const int pan = ifx_window(voices[voice].pan, PAN_MIN, PAN_MAX);
    const int delay = ifx_window(voices[voice].delay, DELAY_MIN, DELAY_MAX);
    ifx_pitch_design_voice(pitch, fine, level, pan, delay,
                           &config->coef[IFX_PITCH_COEF_VOICES + 8u * voice]);
  }
  return 0;
}
