#include "ifx_delay_setup.h"

#include "ifx_dsp_math.h"
#include "ifx_ducker_design.h"
#include "ifx_hush_design.h"
#include "ifx_mixer_design.h"

/* The MCU's windows at EPROM 0x12C8, exclusive maxima. */
enum {
  DELAY_LEVEL_MIN = 0,
  DELAY_LEVEL_MAX = 59,
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
  LEVEL_MIN = 0,
  LEVEL_MAX = 59,
  PAN_MIN = 0,
  PAN_MAX = 101,
  TIME_MIN = 0,
  TIME_MAX = 151,
  REGEN_MIN = 0,
  REGEN_MAX = 59,
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

enum {
  DAMP_STEP = 0x00902E,
  SAMPLES_PER_STEP = 196, /* 5 ms */
};

static const int32_t k_mute_flags[MUTE_TYPE_MAX] = {
    IFX_DELAY_FLAG_MUTE_PRE,
    IFX_DELAY_FLAG_MUTE_POST,
    IFX_DELAY_FLAG_MUTE_PRE | IFX_DELAY_FLAG_MUTE_POST,
};

static const int32_t k_type_flags[DELAY_TYPE_MAX] = {
    IFX_DELAY_FLAG_STEREO,
    IFX_DELAY_FLAG_PINGPONG,
    IFX_DELAY_FLAG_2TAP,
};

/* A time of 0 reads as one step. */
static int32_t time_word(int time) { return SAMPLES_PER_STEP * (time == 0 ? 1 : time); }

int ifx_delay_setup_config(const ifx_context *context, const ifx_delay_setup_raw *raw,
                           ifx_delay_config *config) {
  if (raw == NULL || config == NULL || ifx_context_rate(context) == 0.0) {
    return -1;
  }

  const int delay_level = ifx_window(raw->delay_level, DELAY_LEVEL_MIN, DELAY_LEVEL_MAX);
  const int direct_left = ifx_window(raw->direct_left, DIRECT_LEFT_MIN, DIRECT_LEFT_MAX);
  const int direct_right = ifx_window(raw->direct_right, DIRECT_RIGHT_MIN, DIRECT_RIGHT_MAX);
  const int direct_hush = ifx_window(raw->direct_hush, DIRECT_HUSH_MIN, DIRECT_HUSH_MAX);
  const int hush = ifx_window(raw->hush, HUSH_MIN, HUSH_MAX);
  const int hush_threshold =
      ifx_window(raw->hush_threshold, HUSH_THRESHOLD_MIN, HUSH_THRESHOLD_MAX);
  const int hush_release = ifx_window(raw->hush_release, HUSH_RELEASE_MIN, HUSH_RELEASE_MAX);
  const int delay = ifx_window(raw->delay, DELAY_MIN, DELAY_MAX);
  const int mute_type = ifx_window(raw->mute_type, MUTE_TYPE_MIN, MUTE_TYPE_MAX);
  const int level1 = ifx_window(raw->level1, LEVEL_MIN, LEVEL_MAX);
  const int pan1 = ifx_window(raw->pan1, PAN_MIN, PAN_MAX);
  const int time1 = ifx_window(raw->time1, TIME_MIN, TIME_MAX);
  const int regen1 = ifx_window(raw->regen1, REGEN_MIN, REGEN_MAX);
  const int level2 = ifx_window(raw->level2, LEVEL_MIN, LEVEL_MAX);
  const int pan2 = ifx_window(raw->pan2, PAN_MIN, PAN_MAX);
  const int time2 = ifx_window(raw->time2, TIME_MIN, TIME_MAX);
  const int regen2 = ifx_window(raw->regen2, REGEN_MIN, REGEN_MAX);
  const int delay_type = ifx_window(raw->delay_type, DELAY_TYPE_MIN, DELAY_TYPE_MAX);
  const int hf_damp = ifx_window(raw->hf_damp, HF_DAMP_MIN, HF_DAMP_MAX);
  const int ducker = ifx_window(raw->ducker, DUCKER_MIN, DUCKER_MAX);
  const int ducker_sensitivity =
      ifx_window(raw->ducker_sensitivity, DUCKER_SENSITIVITY_MIN, DUCKER_SENSITIVITY_MAX);
  const int ducker_attenuation =
      ifx_window(raw->ducker_attenuation, DUCKER_ATTENUATION_MIN, DUCKER_ATTENUATION_MAX);
  const int ducker_release =
      ifx_window(raw->ducker_release, DUCKER_RELEASE_MIN, DUCKER_RELEASE_MAX);

  ifx_hush_design_words hush_words;
  ifx_hush_design_words_make(hush_threshold, hush_release, &hush_words);
  ifx_ducker_design_words ducker_words;
  ifx_ducker_design_words_make(ducker_sensitivity, ducker_attenuation, ducker_release,
                               &ducker_words);

  config->coef[IFX_DELAY_COEF_WET] = ifx_mixer_design_level(delay_level);
  config->coef[IFX_DELAY_COEF_DIRECT_LEFT] = ifx_mixer_design_level(direct_left);
  config->coef[IFX_DELAY_COEF_DIRECT_RIGHT] = ifx_mixer_design_level(direct_right);
  config->coef[IFX_DELAY_COEF_FLAGS] = ifx_mixer_design_flags(hush, direct_hush);
  config->coef[IFX_DELAY_COEF_HUSH_THRESHOLD] = hush_words.threshold;
  config->coef[IFX_DELAY_COEF_HUSH_SLOPE] = hush_words.slope;
  config->coef[IFX_DELAY_COEF_HUSH_RELEASE] = hush_words.release;
  config->coef[IFX_DELAY_COEF_DELAY_FLAGS] =
      (delay != 0 ? IFX_DELAY_FLAG_ON : 0) | k_mute_flags[mute_type] | k_type_flags[delay_type];
  config->coef[IFX_DELAY_COEF_DAMP] = ifx_move_a0(ifx_mpy(hf_damp, DAMP_STEP));
  config->coef[IFX_DELAY_COEF_TIME1] = time_word(time1);
  config->coef[IFX_DELAY_COEF_TIME2] = time_word(time2);
  ifx_mixer_design_pan(ifx_mixer_design_level(level1), pan1,
                       &config->coef[IFX_DELAY_COEF_GAIN1_LEFT],
                       &config->coef[IFX_DELAY_COEF_GAIN1_RIGHT]);
  ifx_mixer_design_pan(ifx_mixer_design_level(level2), pan2,
                       &config->coef[IFX_DELAY_COEF_GAIN2_LEFT],
                       &config->coef[IFX_DELAY_COEF_GAIN2_RIGHT]);
  config->coef[IFX_DELAY_COEF_REG_LEFT] = ifx_mixer_design_level(regen1);
  config->coef[IFX_DELAY_COEF_REG_RIGHT] = ifx_mixer_design_level(regen2);
  config->coef[IFX_DELAY_COEF_DUCK_THRESHOLD] = ducker_words.threshold;
  config->coef[IFX_DELAY_COEF_DUCK_ATTEN] = ducker_words.attenuation;
  config->coef[IFX_DELAY_COEF_DUCK_RELEASE] = ducker_words.release;
  config->coef[IFX_DELAY_COEF_DUCKER] = ducker;
  return 0;
}
