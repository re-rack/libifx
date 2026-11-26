#include "ifx_delay.h"
#include "ifx_delay_setup.h"
#include "ifx_effect.h"

#define DELAY_KNOBS 23u
#define DELAY_SETUP_WORDS IFX_DELAY_COEF_COUNT

static const ifx_effect_knob k_knobs[DELAY_KNOBS] = {
    {"DELAY LVL", 0, 58, 48, 0, 40, 0},   {"L DIR LVL", 0, 58, 46, 0, 40, 0},
    {"R DIR LVL", 0, 58, 46, 0, 40, 0},   {"DIRECT HUSH", 0, 1, 0, 0, 1, 0},
    {"HUSH I/O", 0, 1, 1, 0, 1, 0},       {"EXP THRESH", 0, 24, 8, 0, 20, 0},
    {"REL RATE", 0, 9, 5, 0, 5, 0},       {"DELAY", 0, 1, 1, 0, 1, 0},
    {"MUTE TYPE", 0, 2, 0, 0, 0, 0},      {"LEVEL 1", 0, 58, 58, 0, 40, 0},
    {"PAN 1", 0, 100, 0, 0, 0, 0},        {"DLY TIME1", 0, 150, 75, IFX_EFFECT_HOLD, 9, 0},
    {"REGEN 1", 0, 58, 46, 0, 58, 0},     {"LEVEL 2", 0, 58, 58, 0, 40, 0},
    {"PAN 2", 0, 100, 100, 0, 100, 0},    {"DLY TIME2", 0, 150, 50, IFX_EFFECT_HOLD, 6, 0},
    {"REGEN 2", 0, 58, 52, 0, 58, 0},     {"D TYPE", 0, 2, 0, 0, 0, 1},
    {"DL HF DAMP", 0, 99, 12, 0, 12, 0},  {"DUCKER", 0, 1, 1, 0, 1, 0},
    {"SENSITIVITY", 0, 24, 20, 0, 16, 0}, {"ATTENUATION", 0, 58, 19, 0, 30, 0},
    {"RELEASE RATE", 0, 19, 8, 0, 8, 0},
};

static void delay_raw(const int32_t *values, ifx_delay_setup_raw *raw) {
  *raw = (ifx_delay_setup_raw){
      .effect_level = (int)values[0],
      .direct_left = (int)values[1],
      .direct_right = (int)values[2],
      .direct_hush = (int)values[3],
      .hush = (int)values[4],
      .hush_threshold = (int)values[5],
      .hush_release = (int)values[6],
      .delay = (int)values[7],
      .mute_type = (int)values[8],
      .level1 = (int)values[9],
      .pan1 = (int)values[10],
      .time1 = (int)values[11],
      .regen1 = (int)values[12],
      .level2 = (int)values[13],
      .pan2 = (int)values[14],
      .time2 = (int)values[15],
      .regen2 = (int)values[16],
      .delay_type = (int)values[17],
      .hf_damp = (int)values[18],
      .ducker = (int)values[19],
      .ducker_sensitivity = (int)values[20],
      .ducker_attenuation = (int)values[21],
      .ducker_release = (int)values[22],
  };
}

static void delay_words(const ifx_delay_config *config, int32_t *words) {
  for (unsigned word = 0; word < IFX_DELAY_COEF_COUNT; word++) {
    words[word] = config->coef[word];
  }
}

IFX_EFFECT_DEFINE(delay, delay, k_knobs, delay_raw, delay_words, DELAY_SETUP_WORDS, NULL, 0)
