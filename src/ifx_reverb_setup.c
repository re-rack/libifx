#include "ifx_reverb_setup.h"

#include "ifx_analog_design.h"
#include "ifx_dsp_math.h"
#include "ifx_hush_design.h"
#include "ifx_mixer_design.h"
#include "ifx_reverb_net_design.h"
#include "ifx_tables.h"

/* Exclusive maxima: the MCU's windows at EPROM 0x12C8, and the library's
 * ANALOG_DIRECT and OUTPUT_LEVEL. */
enum {
  REVERB_LEVEL_MIN = 0,
  REVERB_LEVEL_MAX = 59,
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
  HUSH_MIN = 0,
  HUSH_MAX = 2,
  HUSH_THRESHOLD_MIN = 0,
  HUSH_THRESHOLD_MAX = 25,
  HUSH_RELEASE_MIN = 0,
  HUSH_RELEASE_MAX = 10,
  REVERB_DECAY_MIN = 0,
  REVERB_DECAY_MAX = 100,
  HF_DAMP_MIN = 0,
  HF_DAMP_MAX = 100,
  LOW_FREQ_MIN = 0,
  LOW_FREQ_MAX = 100,
  REVERB_TYPE_MIN = 0,
  REVERB_TYPE_MAX = 8,
  DIR_IN_PAN_MIN = 0,
  DIR_IN_PAN_MAX = 101,
  PREDELAY_MIN = 0,
  PREDELAY_MAX = 210,
  GATE_MIN = 0,
  GATE_MAX = 2,
  GATE_DECAY_MIN = 0,
  GATE_DECAY_MAX = 32,
  GATE_THRESHOLD_MIN = 0,
  GATE_THRESHOLD_MAX = 25,
  GATE_HOLD_MIN = 0,
  GATE_HOLD_MAX = 100,
};

enum {
  LOW_FREQ_STEP = 0x00A3D7,
  SAMPLES_PER_MS = 39,
};

/* The LOW FREQ word runs from -1.0 at 0 to 0.98 at 99. */
static int32_t low_freq_word(int value) {
  const uint32_t low = (uint32_t)ifx_a0(ifx_mpy(LOW_FREQ_STEP, value));
  return ifx_s24(2u * low + 0x800000u);
}

int ifx_reverb_setup_config(const ifx_reverb_setup_raw *raw, ifx_reverb_config *config) {
  if (raw == NULL || config == NULL) {
    return -1;
  }

  const int reverb_level = ifx_window(raw->reverb_level, REVERB_LEVEL_MIN, REVERB_LEVEL_MAX);
  const int direct_left = ifx_window(raw->direct_left, DIRECT_LEFT_MIN, DIRECT_LEFT_MAX);
  const int direct_right = ifx_window(raw->direct_right, DIRECT_RIGHT_MIN, DIRECT_RIGHT_MAX);
  const int direct_hush = ifx_window(raw->direct_hush, DIRECT_HUSH_MIN, DIRECT_HUSH_MAX);
  const int analog_direct = ifx_window(raw->analog_direct, ANALOG_DIRECT_MIN, ANALOG_DIRECT_MAX);
  const int output_level = ifx_window(raw->output_level, OUTPUT_LEVEL_MIN, OUTPUT_LEVEL_MAX);
  const int hush = ifx_window(raw->hush, HUSH_MIN, HUSH_MAX);
  const int hush_threshold =
      ifx_window(raw->hush_threshold, HUSH_THRESHOLD_MIN, HUSH_THRESHOLD_MAX);
  const int hush_release = ifx_window(raw->hush_release, HUSH_RELEASE_MIN, HUSH_RELEASE_MAX);
  const int reverb_decay = ifx_window(raw->reverb_decay, REVERB_DECAY_MIN, REVERB_DECAY_MAX);
  const int hf_damp = ifx_window(raw->hf_damp, HF_DAMP_MIN, HF_DAMP_MAX);
  const int low_freq = ifx_window(raw->low_freq, LOW_FREQ_MIN, LOW_FREQ_MAX);
  const int reverb_type = ifx_window(raw->reverb_type, REVERB_TYPE_MIN, REVERB_TYPE_MAX);
  const int dir_in_pan = ifx_window(raw->dir_in_pan, DIR_IN_PAN_MIN, DIR_IN_PAN_MAX);
  const int predelay_left = ifx_window(raw->predelay_left, PREDELAY_MIN, PREDELAY_MAX);
  const int predelay_right = ifx_window(raw->predelay_right, PREDELAY_MIN, PREDELAY_MAX);
  const int gate = ifx_window(raw->gate, GATE_MIN, GATE_MAX);
  const int gate_decay = ifx_window(raw->gate_decay, GATE_DECAY_MIN, GATE_DECAY_MAX);
  const int gate_threshold =
      ifx_window(raw->gate_threshold, GATE_THRESHOLD_MIN, GATE_THRESHOLD_MAX);
  const int gate_hold = ifx_window(raw->gate_hold, GATE_HOLD_MIN, GATE_HOLD_MAX);

  ifx_hush_design_words hush_words;
  ifx_hush_design_words_make(hush_threshold, hush_release, &hush_words);
  ifx_reverb_net_design_words reverb_words;
  ifx_reverb_net_design_words_make(reverb_decay, hf_damp, dir_in_pan, &reverb_words);

  config->coef[IFX_REVERB_COEF_WET] = ifx_mixer_design_level(reverb_level);
  config->coef[IFX_REVERB_COEF_DIRECT_LEFT] = ifx_mixer_design_level(direct_left);
  config->coef[IFX_REVERB_COEF_DIRECT_RIGHT] = ifx_mixer_design_level(direct_right);
  config->coef[IFX_REVERB_COEF_FLAGS] =
      ifx_mixer_design_flags(hush, direct_hush) | (gate != 0 ? IFX_REVERB_FLAG_GATE : 0);
  config->coef[IFX_REVERB_COEF_HUSH_THRESHOLD] = hush_words.threshold;
  config->coef[IFX_REVERB_COEF_HUSH_SLOPE] = hush_words.slope;
  config->coef[IFX_REVERB_COEF_HUSH_RELEASE] = hush_words.release;
  config->coef[IFX_REVERB_COEF_DECAY] = reverb_words.decay;
  config->coef[IFX_REVERB_COEF_DAMP] = reverb_words.damp;
  config->coef[IFX_REVERB_COEF_FEEDBACK] = reverb_words.feedback;
  config->coef[IFX_REVERB_COEF_LOW_FREQ] = low_freq_word(low_freq);
  config->coef[IFX_REVERB_COEF_TYPE] = reverb_type;
  config->coef[IFX_REVERB_COEF_PAN_LEFT] = reverb_words.pan_left;
  config->coef[IFX_REVERB_COEF_PAN_RIGHT] = reverb_words.pan_right;
  config->coef[IFX_REVERB_COEF_PREDELAY_LEFT] = SAMPLES_PER_MS * predelay_left;
  config->coef[IFX_REVERB_COEF_PREDELAY_RIGHT] = SAMPLES_PER_MS * predelay_right;
  config->coef[IFX_REVERB_COEF_GATE_DECAY] = ifx_gate_decay_table[gate_decay & 0x1F];
  config->coef[IFX_REVERB_COEF_GATE_THRESHOLD] = ifx_hush_threshold_table[gate_threshold & 0x1F];
  config->coef[IFX_REVERB_COEF_GATE_HOLD] = ifx_gate_hold_table[gate_hold & 0x7F];
  config->direct_latch[0] = ifx_analog_design_latch(analog_direct, direct_hush, direct_left);
  config->direct_latch[1] = ifx_analog_design_latch(analog_direct, direct_hush, direct_right);
  config->output_gain = ifx_analog_design_output(output_level);
  return 0;
}
