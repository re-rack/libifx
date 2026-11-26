#include "ifx_effect.h"
#include "ifx_pitch.h"
#include "ifx_pitch_setup.h"

#define PITCH_KNOBS 29u
#define PITCH_SETUP_WORDS IFX_PITCH_COEF_COUNT

static const ifx_effect_knob k_knobs[PITCH_KNOBS] = {
    {"PSHIFT LVL", 0, 58, 46, 0, 58, 0},
    {"L DIR LVL", 0, 58, 46, 0, 30, 0},
    {"R DIR LVL", 0, 58, 46, 0, 30, 0},
    {"DIRECT HUSH", 0, 1, 0, 0, 1, 0},
    {"REGEN L", 0, 58, 0, 0, 58, 0},
    {"REGEN R", 0, 58, 0, 0, 58, 0},
    {"HUSH I/O", 0, 1, 1, 0, 1, 0},
    {"EXP THRESH", 0, 24, 8, 0, 20, 0},
    {"REL RATE", 0, 9, 5, 0, 5, 0},
    {"PITCH 1", 0, 180, 85, 0, 0, 0},
    {"FINE 1", 0, 40, 20, 0, 0, 0},
    {"LEVEL 1", 0, 58, 58, 0, 58, 0},
    {"PAN 1", 0, 100, 25, 0, 0, 0},
    {"DELAY 1", 0, 209, 0, IFX_EFFECT_HOLD, 1, 0},
    {"PITCH 2", 0, 180, 145, 0, 60, 0},
    {"FINE 2", 0, 40, 20, 0, 20, 0},
    {"LEVEL 2", 0, 58, 58, 0, 58, 0},
    {"PAN 2", 0, 100, 75, 0, 33, 0},
    {"DELAY 2", 0, 209, 0, IFX_EFFECT_HOLD, 2, 0},
    {"PITCH 3", 0, 180, 105, 0, 120, 0},
    {"FINE 3", 0, 40, 20, 0, 40, 0},
    {"LEVEL 3", 0, 58, 58, 0, 58, 0},
    {"PAN 3", 0, 100, 25, 0, 66, 0},
    {"DELAY 3", 0, 209, 0, IFX_EFFECT_HOLD, 3, 0},
    {"PITCH 4", 0, 180, 145, 0, 180, 0},
    {"FINE 4", 0, 40, 36, 0, 20, 0},
    {"LEVEL 4", 0, 58, 58, 0, 58, 0},
    {"PAN 4", 0, 100, 75, 0, 100, 0},
    {"DELAY 4", 0, 209, 0, IFX_EFFECT_HOLD, 209, 0},
};

static void pitch_raw(const int32_t *values, ifx_pitch_setup_raw *raw) {
  *raw = (ifx_pitch_setup_raw){
      .pitch_level = (int)values[0],
      .direct_left = (int)values[1],
      .direct_right = (int)values[2],
      .direct_hush = (int)values[3],
      .regen_left = (int)values[4],
      .regen_right = (int)values[5],
      .hush = (int)values[6],
      .hush_threshold = (int)values[7],
      .hush_release = (int)values[8],
      .pitch1 = (int)values[9],
      .fine1 = (int)values[10],
      .level1 = (int)values[11],
      .pan1 = (int)values[12],
      .delay1 = (int)values[13],
      .pitch2 = (int)values[14],
      .fine2 = (int)values[15],
      .level2 = (int)values[16],
      .pan2 = (int)values[17],
      .delay2 = (int)values[18],
      .pitch3 = (int)values[19],
      .fine3 = (int)values[20],
      .level3 = (int)values[21],
      .pan3 = (int)values[22],
      .delay3 = (int)values[23],
      .pitch4 = (int)values[24],
      .fine4 = (int)values[25],
      .level4 = (int)values[26],
      .pan4 = (int)values[27],
      .delay4 = (int)values[28],
  };
}

static void pitch_words(const ifx_pitch_config *config, int32_t *words) {
  for (unsigned word = 0; word < IFX_PITCH_COEF_COUNT; word++) {
    words[word] = config->coef[word];
  }
}

static const ifx_effect_extra k_extra[] = {
    {9, 0, 64},    {14, 180, 64}, {19, 0, 64},  {24, 180, 64}, {13, 209, 64}, {18, 0, 64},
    {23, 209, 64}, {28, 0, 64},   {9, 180, 64}, {14, 0, 64},   {19, 180, 64}, {24, 0, 64},
};

IFX_EFFECT_DEFINE(pitch, pitch, k_knobs, pitch_raw, pitch_words, PITCH_SETUP_WORDS, k_extra,
                  sizeof k_extra / sizeof k_extra[0])
