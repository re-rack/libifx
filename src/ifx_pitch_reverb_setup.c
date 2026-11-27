#include "ifx_pitch_reverb_setup.h"

#include "ifx_analog_design.h"
#include "ifx_ducker_design.h"
#include "ifx_hush_design.h"
#include "ifx_mixer_design.h"
#include "ifx_pitch_design.h"
#include "ifx_reverb_net_design.h"

/* Exclusive maxima: the MCU's windows at EPROM 0x12C8, and the library's
 * ANALOG_DIRECT and OUTPUT_LEVEL. */
enum {
  EFFECTS_LEVEL_MIN = 0,
  EFFECTS_LEVEL_MAX = 59,
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
  PITCH_LEVEL_MIN = 0,
  PITCH_LEVEL_MAX = 59,
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
  MIX_DIR_DLY_MIN = 0,
  MIX_DIR_DLY_MAX = 101,
  REVERB_DECAY_MIN = 0,
  REVERB_DECAY_MAX = 100,
  HF_DAMP_MIN = 0,
  HF_DAMP_MAX = 100,
};

int ifx_pitch_reverb_setup_config(const ifx_pitch_reverb_setup_raw *raw,
                                  ifx_pitch_reverb_config *config) {
  if (raw == NULL || config == NULL) {
    return -1;
  }

  const int effects_level = ifx_window(raw->effects_level, EFFECTS_LEVEL_MIN, EFFECTS_LEVEL_MAX);
  const int direct_left = ifx_window(raw->direct_left, DIRECT_LEFT_MIN, DIRECT_LEFT_MAX);
  const int direct_right = ifx_window(raw->direct_right, DIRECT_RIGHT_MIN, DIRECT_RIGHT_MAX);
  const int direct_hush = ifx_window(raw->direct_hush, DIRECT_HUSH_MIN, DIRECT_HUSH_MAX);
  const int analog_direct = ifx_window(raw->analog_direct, ANALOG_DIRECT_MIN, ANALOG_DIRECT_MAX);
  const int output_level = ifx_window(raw->output_level, OUTPUT_LEVEL_MIN, OUTPUT_LEVEL_MAX);
  const int pitch_level = ifx_window(raw->pitch_level, PITCH_LEVEL_MIN, PITCH_LEVEL_MAX);
  const int delay_level = ifx_window(raw->delay_level, DELAY_LEVEL_MIN, DELAY_LEVEL_MAX);
  const int reverb_level = ifx_window(raw->reverb_level, REVERB_LEVEL_MIN, REVERB_LEVEL_MAX);
  const int regen_left = ifx_window(raw->regen_left, REGEN_LEFT_MIN, REGEN_LEFT_MAX);
  const int regen_right = ifx_window(raw->regen_right, REGEN_RIGHT_MIN, REGEN_RIGHT_MAX);
  const int hush = ifx_window(raw->hush, HUSH_MIN, HUSH_MAX);
  const int hush_threshold =
      ifx_window(raw->hush_threshold, HUSH_THRESHOLD_MIN, HUSH_THRESHOLD_MAX);
  const int hush_release = ifx_window(raw->hush_release, HUSH_RELEASE_MIN, HUSH_RELEASE_MAX);
  const int ducker = ifx_window(raw->ducker, DUCKER_MIN, DUCKER_MAX);
  const int ducker_sensitivity =
      ifx_window(raw->ducker_sensitivity, DUCKER_SENSITIVITY_MIN, DUCKER_SENSITIVITY_MAX);
  const int ducker_attenuation =
      ifx_window(raw->ducker_attenuation, DUCKER_ATTENUATION_MIN, DUCKER_ATTENUATION_MAX);
  const int ducker_release =
      ifx_window(raw->ducker_release, DUCKER_RELEASE_MIN, DUCKER_RELEASE_MAX);
  const int rev_input = ifx_window(raw->rev_input, REV_INPUT_MIN, REV_INPUT_MAX);
  const int dir_in_pan = ifx_window(raw->dir_in_pan, DIR_IN_PAN_MIN, DIR_IN_PAN_MAX);
  const int mix_dir_dly = ifx_window(raw->mix_dir_dly, MIX_DIR_DLY_MIN, MIX_DIR_DLY_MAX);
  const int reverb_decay = ifx_window(raw->reverb_decay, REVERB_DECAY_MIN, REVERB_DECAY_MAX);
  const int hf_damp = ifx_window(raw->hf_damp, HF_DAMP_MIN, HF_DAMP_MAX);

  ifx_hush_design_words hush_words;
  ifx_hush_design_words_make(hush_threshold, hush_release, &hush_words);
  ifx_ducker_design_words ducker_words;
  ifx_ducker_design_words_make(ducker_sensitivity, ducker_attenuation, ducker_release,
                               &ducker_words);
  ifx_reverb_net_design_words reverb_words;
  ifx_reverb_net_design_words_make(reverb_decay, hf_damp, dir_in_pan, &reverb_words);

  config->coef[IFX_PITCH_REVERB_COEF_WET] = ifx_mixer_design_level(effects_level);
  config->coef[IFX_PITCH_REVERB_COEF_DIRECT_LEFT] = ifx_mixer_design_level(direct_left);
  config->coef[IFX_PITCH_REVERB_COEF_DIRECT_RIGHT] = ifx_mixer_design_level(direct_right);
  config->coef[IFX_PITCH_REVERB_COEF_FLAGS] =
      ifx_mixer_design_flags(hush, direct_hush) |
      (rev_input != 0 ? IFX_PITCH_REVERB_FLAG_REV_INPUT : 0);
  config->coef[IFX_PITCH_REVERB_COEF_PITCH_LEVEL] = ifx_mixer_design_level(pitch_level);
  config->coef[IFX_PITCH_REVERB_COEF_DELAY_LEVEL] = ifx_mixer_design_level(delay_level);
  config->coef[IFX_PITCH_REVERB_COEF_REVERB_LEVEL] = ifx_mixer_design_level(reverb_level);
  config->coef[IFX_PITCH_REVERB_COEF_REG_LEFT] = ifx_mixer_design_level(regen_left);
  config->coef[IFX_PITCH_REVERB_COEF_REG_RIGHT] = ifx_mixer_design_level(regen_right);
  config->coef[IFX_PITCH_REVERB_COEF_HUSH_THRESHOLD] = hush_words.threshold;
  config->coef[IFX_PITCH_REVERB_COEF_HUSH_SLOPE] = hush_words.slope;
  config->coef[IFX_PITCH_REVERB_COEF_HUSH_RELEASE] = hush_words.release;
  config->coef[IFX_PITCH_REVERB_COEF_DUCKER] = ducker;
  config->coef[IFX_PITCH_REVERB_COEF_DUCK_THRESHOLD] = ducker_words.threshold;
  config->coef[IFX_PITCH_REVERB_COEF_DUCK_ATTEN] = ducker_words.attenuation;
  config->coef[IFX_PITCH_REVERB_COEF_DUCK_RELEASE] = ducker_words.release;
  config->coef[IFX_PITCH_REVERB_COEF_PAN_LEFT] = reverb_words.pan_left;
  config->coef[IFX_PITCH_REVERB_COEF_PAN_RIGHT] = reverb_words.pan_right;
  ifx_reverb_net_design_mix(mix_dir_dly, &config->coef[IFX_PITCH_REVERB_COEF_MIX_LEFT],
                            &config->coef[IFX_PITCH_REVERB_COEF_MIX_RIGHT]);
  config->coef[IFX_PITCH_REVERB_COEF_FEEDBACK] = reverb_words.feedback;
  config->coef[IFX_PITCH_REVERB_COEF_DAMP] = reverb_words.damp;

  const struct {
    int pitch;
    int fine;
    int level;
    int pan;
    int delay;
  } voices[2] = {
      {raw->pitch1, raw->fine1, raw->level1, raw->pan1, raw->delay1},
      {raw->pitch2, raw->fine2, raw->level2, raw->pan2, raw->delay2},
  };
  for (unsigned voice = 0; voice < 2u; voice++) {
    const int pitch = ifx_window(voices[voice].pitch, PITCH_MIN, PITCH_MAX);
    const int fine = ifx_window(voices[voice].fine, FINE_MIN, FINE_MAX);
    const int level = ifx_window(voices[voice].level, LEVEL_MIN, LEVEL_MAX);
    const int pan = ifx_window(voices[voice].pan, PAN_MIN, PAN_MAX);
    const int delay = ifx_window(voices[voice].delay, DELAY_MIN, DELAY_MAX);
    ifx_pitch_design_voice(pitch, fine, level, pan, delay,
                           &config->coef[IFX_PITCH_REVERB_COEF_VOICES + 8u * voice]);
  }

  const int delay3 = ifx_window(raw->delay3, DELAY_MIN, DELAY_MAX);
  int32_t *const delay_voice = &config->coef[IFX_PITCH_REVERB_COEF_VOICES + 16u];
  ifx_pitch_design_voice(0, 0, ifx_window(raw->level3, LEVEL_MIN, LEVEL_MAX),
                         ifx_window(raw->pan3, PAN_MIN, PAN_MAX), delay3, delay_voice);
  delay_voice[0] = 0;
  delay_voice[1] = 0;
  delay_voice[2] = 0;
  config->direct_latch[0] = ifx_analog_design_latch(analog_direct, direct_hush, direct_left);
  config->direct_latch[1] = ifx_analog_design_latch(analog_direct, direct_hush, direct_right);
  config->output_gain = ifx_analog_design_output(output_level);
  return 0;
}
