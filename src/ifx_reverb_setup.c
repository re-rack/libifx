#include "ifx_reverb_setup.h"

#include "ifx_dsp_math.h"
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
  DECAY_MIN = 0,
  DECAY_MAX = 100,
  HF_DAMP_MIN = 0,
  HF_DAMP_MAX = 100,
  LOW_FREQ_MIN = 0,
  LOW_FREQ_MAX = 100,
  TYPE_MIN = 0,
  TYPE_MAX = 8,
  PAN_MIN = 0,
  PAN_MAX = 101,
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

static int clamp(int value, int minimum, int maximum) {
  return value < minimum ? minimum : value > maximum ? maximum : value;
}

static int32_t decay_word(int value) {
  const int32_t low = ifx_s24((uint32_t)ifx_a0(ifx_mpy(0x004189, value)));
  return ifx_s24((uint32_t)(0x4CCCCDu + (uint32_t)low));
}

static int32_t damp_word(int value) { return ifx_s24((uint32_t)ifx_a0(ifx_mpy(0x004189, value))); }

static int32_t low_freq_word(int value) {
  const uint32_t low = (uint32_t)ifx_a0(ifx_mpy(0x00A3D7, value));
  return ifx_s24(2u * low + 0x800000u);
}

int ifx_reverb_setup_config(const ifx_context *context, const ifx_reverb_setup_raw *raw,
                            ifx_reverb_config *config) {
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
  const int decay = clamp(raw->reverb_decay, DECAY_MIN, DECAY_MAX - 1);
  const int hf_damp = clamp(raw->hf_damp, HF_DAMP_MIN, HF_DAMP_MAX - 1);
  const int low_freq = clamp(raw->low_freq, LOW_FREQ_MIN, LOW_FREQ_MAX - 1);
  const int type = clamp(raw->reverb_type, TYPE_MIN, TYPE_MAX - 1);
  const int pan = clamp(raw->dir_in_pan, PAN_MIN, PAN_MAX - 1);
  const int predelay_left = clamp(raw->predelay_left, PREDELAY_MIN, PREDELAY_MAX - 1);
  const int predelay_right = clamp(raw->predelay_right, PREDELAY_MIN, PREDELAY_MAX - 1);
  const int gate = clamp(raw->gate, GATE_MIN, GATE_MAX - 1);
  const int gate_decay = clamp(raw->gate_decay, GATE_DECAY_MIN, GATE_DECAY_MAX - 1);
  const int gate_threshold = clamp(raw->gate_threshold, GATE_THRESHOLD_MIN, GATE_THRESHOLD_MAX - 1);
  const int gate_hold = clamp(raw->gate_hold, GATE_HOLD_MIN, GATE_HOLD_MAX - 1);

  ifx_hush_design_words hush_words;
  ifx_hush_design_words_make(hush_threshold, hush_release, &hush_words);

  unsigned x_flags = 0;
  if (hush != 0) {
    x_flags |= IFX_MIXER_FLAG_HUSH;
  }
  if (direct_hush != 0) {
    x_flags |= IFX_MIXER_FLAG_DIRECT;
  }
  if (gate != 0) {
    x_flags |= IFX_REVERB_FLAG_GATE;
  }

  const int32_t decay_value = decay_word(decay);
  const int32_t damp = damp_word(hf_damp);

  config->coef[IFX_REVERB_COEF_WET] = ifx_mixer_design_level(effect_level);
  config->coef[IFX_REVERB_COEF_DIRECT_LEFT] = ifx_mixer_design_level(direct_left);
  config->coef[IFX_REVERB_COEF_DIRECT_RIGHT] = ifx_mixer_design_level(direct_right);
  config->coef[IFX_REVERB_COEF_FLAGS] = (int32_t)x_flags;
  config->coef[IFX_REVERB_COEF_HUSH_THRESHOLD] = hush_words.threshold;
  config->coef[IFX_REVERB_COEF_HUSH_SLOPE] = hush_words.slope;
  config->coef[IFX_REVERB_COEF_HUSH_RELEASE] = hush_words.release;
  config->coef[IFX_REVERB_COEF_DECAY] = decay_value;
  config->coef[IFX_REVERB_COEF_DAMP] = damp;
  config->coef[IFX_REVERB_COEF_FEEDBACK] =
      ifx_store24(ifx_macr(ifx_word_acc(decay_value), -damp, decay_value));
  config->coef[IFX_REVERB_COEF_LOW_FREQ] = low_freq_word(low_freq);
  config->coef[IFX_REVERB_COEF_TYPE] = type;
  config->coef[IFX_REVERB_COEF_PAN_LEFT] = ifx_pan_table[pan & 0x7F];
  config->coef[IFX_REVERB_COEF_PAN_RIGHT] = ifx_pan_table[(100 - pan) & 0x7F];
  config->coef[IFX_REVERB_COEF_PREDELAY_LEFT] = 39 * predelay_left;
  config->coef[IFX_REVERB_COEF_PREDELAY_RIGHT] = 39 * predelay_right;
  config->coef[IFX_REVERB_COEF_GATE_DECAY] = ifx_gate_decay_table[gate_decay & 0x1F];
  config->coef[IFX_REVERB_COEF_GATE_THRESHOLD] = ifx_hush_threshold_table[gate_threshold & 0x1F];
  config->coef[IFX_REVERB_COEF_GATE_HOLD] = ifx_gate_hold_table[gate_hold & 0x7F];
  return 0;
}
