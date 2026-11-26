#include "ifx_delay_setup.h"

#include "ifx_dsp_math.h"
#include "ifx_ducker_design.h"
#include "ifx_hush_design.h"
#include "ifx_mixer_design.h"
#include "ifx_tables.h"

/* The MCU's windows at EPROM 0x12C8, exclusive maxima. */
enum {
  EFFECT_LEVEL_MIN = 0,
  EFFECT_LEVEL_MAX = 59,
  DIRECT_LEFT_MIN = 0,
  DIRECT_LEFT_MAX = 59,
  DIRECT_RIGHT_MIN = 0,
  DIRECT_RIGHT_MAX = 59,
  DIRECT_HUSH_MIN = 0,
  DIRECT_HUSH_MAX = 2,
  HUSH_MIN = 0,
  HUSH_MAX = 2,
  HUSH_THRESHOLD_MIN = 0,
  HUSH_THRESHOLD_MAX = 25,
  HUSH_RELEASE_MIN = 0,
  HUSH_RELEASE_MAX = 10,
  DELAY_MIN = 0,
  DELAY_MAX = 2,
  MUTE_TYPE_MIN = 0,
  MUTE_TYPE_MAX = 3,
  LEVEL1_MIN = 0,
  LEVEL1_MAX = 59,
  PAN1_MIN = 0,
  PAN1_MAX = 101,
  TIME1_MIN = 0,
  TIME1_MAX = 151,
  REGEN1_MIN = 0,
  REGEN1_MAX = 59,
  LEVEL2_MIN = 0,
  LEVEL2_MAX = 59,
  PAN2_MIN = 0,
  PAN2_MAX = 101,
  TIME2_MIN = 0,
  TIME2_MAX = 151,
  REGEN2_MIN = 0,
  REGEN2_MAX = 59,
  DELAY_TYPE_MIN = 0,
  DELAY_TYPE_MAX = 3,
  HF_DAMP_MIN = 0,
  HF_DAMP_MAX = 100,
  DUCKER_MIN = 0,
  DUCKER_MAX = 2,
  DUCKER_SENSITIVITY_MIN = 0,
  DUCKER_SENSITIVITY_MAX = 25,
  DUCKER_ATTENUATION_MIN = 0,
  DUCKER_ATTENUATION_MAX = 59,
  DUCKER_RELEASE_MIN = 0,
  DUCKER_RELEASE_MAX = 20,
};

static int clamp(int value, int minimum, int maximum) {
  return value < minimum ? minimum : value > maximum ? maximum : value;
}

static int32_t pan_gain(int32_t level, int pan) {
  return ifx_store24(ifx_mpyr(level, ifx_pan_table[pan & 0x7F]));
}

int ifx_delay_setup_config(const ifx_context *context, const ifx_delay_setup_raw *raw,
                           ifx_delay_config *config) {
  if (raw == NULL || config == NULL || ifx_context_rate(context) == 0.0) {
    return -1;
  }

  const int effect_level = clamp(raw->effect_level, EFFECT_LEVEL_MIN, EFFECT_LEVEL_MAX - 1);
  const int direct_left = clamp(raw->direct_left, DIRECT_LEFT_MIN, DIRECT_LEFT_MAX - 1);
  const int direct_right = clamp(raw->direct_right, DIRECT_RIGHT_MIN, DIRECT_RIGHT_MAX - 1);
  const int direct_hush = clamp(raw->direct_hush, DIRECT_HUSH_MIN, DIRECT_HUSH_MAX - 1);
  const int hush = clamp(raw->hush, HUSH_MIN, HUSH_MAX - 1);
  const int hush_threshold = clamp(raw->hush_threshold, HUSH_THRESHOLD_MIN, HUSH_THRESHOLD_MAX - 1);
  const int hush_release = clamp(raw->hush_release, HUSH_RELEASE_MIN, HUSH_RELEASE_MAX - 1);
  const int delay = clamp(raw->delay, DELAY_MIN, DELAY_MAX - 1);
  const int mute_type = clamp(raw->mute_type, MUTE_TYPE_MIN, MUTE_TYPE_MAX - 1);
  const int level1 = clamp(raw->level1, LEVEL1_MIN, LEVEL1_MAX - 1);
  const int pan1 = clamp(raw->pan1, PAN1_MIN, PAN1_MAX - 1);
  const int time1 = clamp(raw->time1, TIME1_MIN, TIME1_MAX - 1);
  const int regen1 = clamp(raw->regen1, REGEN1_MIN, REGEN1_MAX - 1);
  const int level2 = clamp(raw->level2, LEVEL2_MIN, LEVEL2_MAX - 1);
  const int pan2 = clamp(raw->pan2, PAN2_MIN, PAN2_MAX - 1);
  const int time2 = clamp(raw->time2, TIME2_MIN, TIME2_MAX - 1);
  const int regen2 = clamp(raw->regen2, REGEN2_MIN, REGEN2_MAX - 1);
  const int delay_type = clamp(raw->delay_type, DELAY_TYPE_MIN, DELAY_TYPE_MAX - 1);
  const int hf_damp = clamp(raw->hf_damp, HF_DAMP_MIN, HF_DAMP_MAX - 1);
  const int ducker = clamp(raw->ducker, DUCKER_MIN, DUCKER_MAX - 1);
  const int sensitivity =
      clamp(raw->ducker_sensitivity, DUCKER_SENSITIVITY_MIN, DUCKER_SENSITIVITY_MAX - 1);
  const int attenuation =
      clamp(raw->ducker_attenuation, DUCKER_ATTENUATION_MIN, DUCKER_ATTENUATION_MAX - 1);
  const int ducker_release = clamp(raw->ducker_release, DUCKER_RELEASE_MIN, DUCKER_RELEASE_MAX - 1);

  ifx_hush_design_words hush_words;
  ifx_hush_design_words_make(hush_threshold, hush_release, &hush_words);
  ifx_ducker_design_words ducker_words;
  ifx_ducker_design_words_make(sensitivity, attenuation, ducker_release, &ducker_words);

  unsigned x_flags = 0;
  if (hush != 0) {
    x_flags |= IFX_MIXER_FLAG_HUSH;
  }
  if (direct_hush != 0) {
    x_flags |= IFX_MIXER_FLAG_DIRECT;
  }
  int32_t delay_flags = 0;
  if (delay != 0) {
    delay_flags |= IFX_DELAY_ON;
  }
  if (mute_type == 0) {
    delay_flags |= IFX_DELAY_MUTE_PRE;
  } else if (mute_type == 1) {
    delay_flags |= IFX_DELAY_MUTE_POST;
  } else {
    delay_flags |= IFX_DELAY_MUTE_PRE | IFX_DELAY_MUTE_POST;
  }
  if (delay_type == 0) {
    delay_flags |= IFX_DELAY_TYPE_STEREO;
  } else if (delay_type == 1) {
    delay_flags |= IFX_DELAY_TYPE_PINGPONG;
  } else {
    delay_flags |= IFX_DELAY_TYPE_2TAP;
  }

  const int32_t level1_word = ifx_mixer_design_level(level1);
  const int32_t level2_word = ifx_mixer_design_level(level2);
  const int32_t delay1 = 196 * (time1 == 0 ? 1 : time1);
  const int32_t delay2 = 196 * (time2 == 0 ? 1 : time2);

  config->coef[IFX_DELAY_COEF_WET] = ifx_mixer_design_level(effect_level);
  config->coef[IFX_DELAY_COEF_DIRECT_LEFT] = ifx_mixer_design_level(direct_left);
  config->coef[IFX_DELAY_COEF_DIRECT_RIGHT] = ifx_mixer_design_level(direct_right);
  config->coef[IFX_DELAY_COEF_FLAGS] = (int32_t)x_flags;
  config->coef[IFX_DELAY_COEF_HUSH_THRESHOLD] = hush_words.threshold;
  config->coef[IFX_DELAY_COEF_HUSH_SLOPE] = hush_words.slope;
  config->coef[IFX_DELAY_COEF_HUSH_RELEASE] = hush_words.release;
  config->coef[IFX_DELAY_COEF_DELAY_FLAGS] = delay_flags;
  config->coef[IFX_DELAY_COEF_DAMP] = ifx_a0(ifx_mpy(hf_damp, 0x00902E));
  config->coef[IFX_DELAY_COEF_TIME1] = delay1;
  config->coef[IFX_DELAY_COEF_TIME2] = delay2;
  config->coef[IFX_DELAY_COEF_GAIN1_LEFT] = pan_gain(level1_word, pan1);
  config->coef[IFX_DELAY_COEF_GAIN1_RIGHT] = pan_gain(level1_word, 100 - pan1);
  config->coef[IFX_DELAY_COEF_GAIN2_LEFT] = pan_gain(level2_word, pan2);
  config->coef[IFX_DELAY_COEF_GAIN2_RIGHT] = pan_gain(level2_word, 100 - pan2);
  config->coef[IFX_DELAY_COEF_REG_LEFT] = ifx_mixer_design_level(regen1);
  config->coef[IFX_DELAY_COEF_REG_RIGHT] = ifx_mixer_design_level(regen2);
  config->coef[IFX_DELAY_COEF_DUCK_THRESHOLD] = ducker_words.threshold;
  config->coef[IFX_DELAY_COEF_DUCK_ATTEN] = ducker_words.attenuation;
  config->coef[IFX_DELAY_COEF_DUCK_RELEASE] = ducker_words.release;
  config->coef[IFX_DELAY_COEF_DUCKER] = ducker;
  return 0;
}
