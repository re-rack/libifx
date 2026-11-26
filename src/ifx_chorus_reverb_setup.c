#include "ifx_chorus_reverb_setup.h"

#include "ifx_chorus_design.h"
#include "ifx_ducker_design.h"
#include "ifx_hush_design.h"
#include "ifx_mixer_design.h"
#include "ifx_reverb_net_design.h"

/* The MCU's windows at EPROM 0x12C8, exclusive maxima. */
enum {
  EFFECTS_LEVEL_MIN = 0,
  EFFECTS_LEVEL_MAX = 59,
  DIRECT_LEFT_MIN = 0,
  DIRECT_LEFT_MAX = 59,
  DIRECT_RIGHT_MIN = 0,
  DIRECT_RIGHT_MAX = 59,
  DIRECT_HUSH_MIN = 0,
  DIRECT_HUSH_MAX = 2,
  CHORUS_LEVEL_MIN = 0,
  CHORUS_LEVEL_MAX = 59,
  DELAY_LEVEL_MIN = 0,
  DELAY_LEVEL_MAX = 59,
  REVERB_LEVEL_MIN = 0,
  REVERB_LEVEL_MAX = 59,
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
  DUCKER_MIN = 0,
  DUCKER_MAX = 4,
  DUCKER_SENSITIVITY_MIN = 0,
  DUCKER_SENSITIVITY_MAX = 25,
  DUCKER_ATTENUATION_MIN = 0,
  DUCKER_ATTENUATION_MAX = 59,
  DUCKER_RELEASE_MIN = 0,
  DUCKER_RELEASE_MAX = 20,
  REV_INPUT_MIN = 0,
  REV_INPUT_MAX = 2,
  DIR_IN_PAN_MIN = 0,
  DIR_IN_PAN_MAX = 101,
  MIX_MIN = 0,
  MIX_MAX = 101,
  DECAY_MIN = 0,
  DECAY_MAX = 100,
  HF_DAMP_MIN = 0,
  HF_DAMP_MAX = 100,
};

static int clamp(int value, int minimum, int maximum) {
  return value < minimum ? minimum : value > maximum ? maximum : value;
}

int ifx_chorus_reverb_setup_config(const ifx_context *context,
                                   const ifx_chorus_reverb_setup_raw *raw,
                                   ifx_chorus_reverb_config *config) {
  if (raw == NULL || config == NULL || ifx_context_rate(context) == 0.0) {
    return -1;
  }

  const int effects_level = clamp(raw->effects_level, EFFECTS_LEVEL_MIN, EFFECTS_LEVEL_MAX - 1);
  const int direct_left = clamp(raw->direct_left, DIRECT_LEFT_MIN, DIRECT_LEFT_MAX - 1);
  const int direct_right = clamp(raw->direct_right, DIRECT_RIGHT_MIN, DIRECT_RIGHT_MAX - 1);
  const int direct_hush = clamp(raw->direct_hush, DIRECT_HUSH_MIN, DIRECT_HUSH_MAX - 1);
  const int chorus_level = clamp(raw->chorus_level, CHORUS_LEVEL_MIN, CHORUS_LEVEL_MAX - 1);
  const int delay_level = clamp(raw->delay_level, DELAY_LEVEL_MIN, DELAY_LEVEL_MAX - 1);
  const int reverb_level = clamp(raw->reverb_level, REVERB_LEVEL_MIN, REVERB_LEVEL_MAX - 1);
  const int regen_left = clamp(raw->regen_left, REGEN_LEFT_MIN, REGEN_LEFT_MAX - 1);
  const int regen_right = clamp(raw->regen_right, REGEN_RIGHT_MIN, REGEN_RIGHT_MAX - 1);
  const int hush = clamp(raw->hush, HUSH_MIN, HUSH_MAX - 1);
  const int hush_threshold = clamp(raw->hush_threshold, HUSH_THRESHOLD_MIN, HUSH_THRESHOLD_MAX - 1);
  const int hush_release = clamp(raw->hush_release, HUSH_RELEASE_MIN, HUSH_RELEASE_MAX - 1);
  const int ducker = clamp(raw->ducker, DUCKER_MIN, DUCKER_MAX - 1);
  const int sensitivity =
      clamp(raw->ducker_sensitivity, DUCKER_SENSITIVITY_MIN, DUCKER_SENSITIVITY_MAX - 1);
  const int attenuation =
      clamp(raw->ducker_attenuation, DUCKER_ATTENUATION_MIN, DUCKER_ATTENUATION_MAX - 1);
  const int ducker_release = clamp(raw->ducker_release, DUCKER_RELEASE_MIN, DUCKER_RELEASE_MAX - 1);
  const int rev_input = clamp(raw->rev_input, REV_INPUT_MIN, REV_INPUT_MAX - 1);
  const int dir_in_pan = clamp(raw->dir_in_pan, DIR_IN_PAN_MIN, DIR_IN_PAN_MAX - 1);
  const int mix_dir_dly = clamp(raw->mix_dir_dly, MIX_MIN, MIX_MAX - 1);
  const int decay = clamp(raw->reverb_decay, DECAY_MIN, DECAY_MAX - 1);
  const int hf_damp = clamp(raw->hf_damp, HF_DAMP_MIN, HF_DAMP_MAX - 1);

  ifx_hush_design_words hush_words;
  ifx_hush_design_words_make(hush_threshold, hush_release, &hush_words);
  ifx_ducker_design_words ducker_words;
  ifx_ducker_design_words_make(sensitivity, attenuation, ducker_release, &ducker_words);
  ifx_reverb_net_design_words net_words;
  ifx_reverb_net_design_words_make(decay, hf_damp, dir_in_pan, mix_dir_dly, &net_words);

  unsigned x_flags = 0;
  if (hush != 0) {
    x_flags |= IFX_MIXER_FLAG_HUSH;
  }
  if (direct_hush != 0) {
    x_flags |= IFX_MIXER_FLAG_DIRECT;
  }
  if (rev_input != 0) {
    x_flags |= IFX_CHORUS_REVERB_FLAG_REV_INPUT;
  }

  config->coef[IFX_CHORUS_REVERB_COEF_WET] = ifx_mixer_design_level(effects_level);
  config->coef[IFX_CHORUS_REVERB_COEF_DIRECT_LEFT] = ifx_mixer_design_level(direct_left);
  config->coef[IFX_CHORUS_REVERB_COEF_DIRECT_RIGHT] = ifx_mixer_design_level(direct_right);
  config->coef[IFX_CHORUS_REVERB_COEF_FLAGS] = (int32_t)x_flags;
  config->coef[IFX_CHORUS_REVERB_COEF_CHORUS_LEVEL] = ifx_mixer_design_level(chorus_level);
  config->coef[IFX_CHORUS_REVERB_COEF_DELAY_LEVEL] = ifx_mixer_design_level(delay_level);
  config->coef[IFX_CHORUS_REVERB_COEF_REVERB_LEVEL] = ifx_mixer_design_level(reverb_level);
  config->coef[IFX_CHORUS_REVERB_COEF_REG_LEFT] = ifx_mixer_design_level(regen_left);
  config->coef[IFX_CHORUS_REVERB_COEF_REG_RIGHT] = ifx_mixer_design_level(regen_right);
  config->coef[IFX_CHORUS_REVERB_COEF_HUSH_THRESHOLD] = hush_words.threshold;
  config->coef[IFX_CHORUS_REVERB_COEF_HUSH_SLOPE] = hush_words.slope;
  config->coef[IFX_CHORUS_REVERB_COEF_HUSH_RELEASE] = hush_words.release;
  config->coef[IFX_CHORUS_REVERB_COEF_DUCKER] = ducker;
  config->coef[IFX_CHORUS_REVERB_COEF_DUCK_THRESHOLD] = ducker_words.threshold;
  config->coef[IFX_CHORUS_REVERB_COEF_DUCK_ATTEN] = ducker_words.attenuation;
  config->coef[IFX_CHORUS_REVERB_COEF_DUCK_RELEASE] = ducker_words.release;
  config->coef[IFX_CHORUS_REVERB_COEF_PAN_LEFT] = net_words.pan_left;
  config->coef[IFX_CHORUS_REVERB_COEF_PAN_RIGHT] = net_words.pan_right;
  config->coef[IFX_CHORUS_REVERB_COEF_MIX_LEFT] = net_words.mix_left;
  config->coef[IFX_CHORUS_REVERB_COEF_MIX_RIGHT] = net_words.mix_right;
  config->coef[IFX_CHORUS_REVERB_COEF_FEEDBACK] = net_words.feedback;
  config->coef[IFX_CHORUS_REVERB_COEF_DAMP] = net_words.damp;

  const struct {
    int level;
    int pan;
    int delay;
    int depth;
    int rate;
  } voices[4] = {
      {raw->level1, raw->pan1, raw->delay1, raw->depth1, raw->rate1},
      {raw->level2, raw->pan2, raw->delay2, raw->depth2, raw->rate2},
      {raw->level3, raw->pan3, raw->delay3, raw->depth3, raw->rate3},
      {raw->level4, raw->pan4, raw->delay4, raw->depth4, raw->rate4},
  };
  for (unsigned voice = 0; voice < 4u; voice++) {
    const int level = clamp(voices[voice].level, LEVEL_MIN, LEVEL_MAX - 1);
    const int pan = clamp(voices[voice].pan, PAN_MIN, PAN_MAX - 1);
    const int delay = clamp(voices[voice].delay, DELAY_MIN, DELAY_MAX - 1);
    const int depth = clamp(voices[voice].depth, DEPTH_MIN, DEPTH_MAX - 1);
    const int rate = clamp(voices[voice].rate, RATE_MIN, RATE_MAX - 1);
    ifx_chorus_design_voice(level, pan, delay, depth, rate,
                            &config->coef[IFX_CHORUS_REVERB_COEF_VOICES + 8u * voice]);
  }
  return 0;
}
