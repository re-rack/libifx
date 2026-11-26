#include "ifx_chorus8.h"
#include "ifx_chorus8_setup.h"
#include "ifx_effect.h"

#define CHORUS8_KNOBS 49u
#define CHORUS8_SETUP_WORDS IFX_CHORUS8_COEF_COUNT

static const ifx_effect_knob k_knobs[CHORUS8_KNOBS] = {
    {"CHORUS LVL", 0, 58, 44, 0, 58, 0},
    {"L DIR LVL", 0, 58, 42, 0, 30, 0},
    {"R DIR LVL", 0, 58, 42, 0, 30, 0},
    {"DIRECT HUSH", 0, 1, 0, 0, 1, 0},
    {"REGEN L", 0, 58, 0, 0, 58, 0},
    {"REGEN R", 0, 58, 3, 0, 58, 0},
    {"HUSH I/O", 0, 1, 1, 0, 1, 0},
    {"EXP THRESH", 0, 24, 8, 0, 20, 0},
    {"REL RATE", 0, 9, 5, 0, 5, 0},
    {"LEVEL 1", 0, 58, 58, 0, 58, 0},
    {"PAN 1", 0, 100, 0, 0, 0, 0},
    {"DELAY 1", 0, 209, 5, IFX_EFFECT_HOLD, 2, 0},
    {"DEPTH 1", 0, 100, 41, 0, 60, 0},
    {"RATE 1", 0, 254, 25, 0, 20, 0},
    {"LEVEL 2", 0, 58, 58, 0, 58, 0},
    {"PAN 2", 0, 100, 0, 0, 15, 0},
    {"DELAY 2", 0, 209, 20, IFX_EFFECT_HOLD, 4, 0},
    {"DEPTH 2", 0, 100, 41, 0, 65, 0},
    {"RATE 2", 0, 254, 21, 0, 30, 0},
    {"LEVEL 3", 0, 58, 58, 0, 58, 0},
    {"PAN 3", 0, 100, 0, 0, 30, 0},
    {"DELAY 3", 0, 209, 30, IFX_EFFECT_HOLD, 6, 0},
    {"DEPTH 3", 0, 100, 36, 0, 70, 0},
    {"RATE 3", 0, 254, 5, 0, 40, 0},
    {"LEVEL 4", 0, 58, 58, 0, 58, 0},
    {"PAN 4", 0, 100, 0, 0, 45, 0},
    {"DELAY 4", 0, 209, 60, IFX_EFFECT_HOLD, 8, 0},
    {"DEPTH 4", 0, 100, 39, 0, 75, 0},
    {"RATE 4", 0, 254, 73, 0, 50, 0},
    {"LEVEL 5", 0, 58, 58, 0, 58, 0},
    {"PAN 5", 0, 100, 100, 0, 60, 0},
    {"DELAY 5", 0, 209, 30, IFX_EFFECT_HOLD, 10, 0},
    {"DEPTH 5", 0, 100, 40, 0, 80, 0},
    {"RATE 5", 0, 254, 38, 0, 60, 0},
    {"LEVEL 6", 0, 58, 58, 0, 58, 0},
    {"PAN 6", 0, 100, 100, 0, 75, 0},
    {"DELAY 6", 0, 209, 120, IFX_EFFECT_HOLD, 12, 0},
    {"DEPTH 6", 0, 100, 43, 0, 85, 0},
    {"RATE 6", 0, 254, 42, 0, 70, 0},
    {"LEVEL 7", 0, 58, 46, 0, 58, 0},
    {"PAN 7", 0, 100, 100, 0, 90, 0},
    {"DELAY 7", 0, 209, 140, IFX_EFFECT_HOLD, 14, 0},
    {"DEPTH 7", 0, 100, 37, 0, 90, 0},
    {"RATE 7", 0, 254, 10, 0, 80, 0},
    {"LEVEL 8", 0, 58, 34, 0, 58, 0},
    {"PAN 8", 0, 100, 100, 0, 100, 0},
    {"DELAY 8", 0, 209, 160, IFX_EFFECT_HOLD, 16, 0},
    {"DEPTH 8", 0, 100, 32, 0, 100, 0},
    {"RATE 8", 0, 254, 35, 0, 90, 0},
};

static void chorus8_raw(const int32_t *values, ifx_chorus8_setup_raw *raw) {
  *raw = (ifx_chorus8_setup_raw){
      .chorus_level = (int)values[0],
      .direct_left = (int)values[1],
      .direct_right = (int)values[2],
      .direct_hush = (int)values[3],
      .regen_left = (int)values[4],
      .regen_right = (int)values[5],
      .hush = (int)values[6],
      .hush_threshold = (int)values[7],
      .hush_release = (int)values[8],
      .level1 = (int)values[9],
      .pan1 = (int)values[10],
      .delay1 = (int)values[11],
      .depth1 = (int)values[12],
      .rate1 = (int)values[13],
      .level2 = (int)values[14],
      .pan2 = (int)values[15],
      .delay2 = (int)values[16],
      .depth2 = (int)values[17],
      .rate2 = (int)values[18],
      .level3 = (int)values[19],
      .pan3 = (int)values[20],
      .delay3 = (int)values[21],
      .depth3 = (int)values[22],
      .rate3 = (int)values[23],
      .level4 = (int)values[24],
      .pan4 = (int)values[25],
      .delay4 = (int)values[26],
      .depth4 = (int)values[27],
      .rate4 = (int)values[28],
      .level5 = (int)values[29],
      .pan5 = (int)values[30],
      .delay5 = (int)values[31],
      .depth5 = (int)values[32],
      .rate5 = (int)values[33],
      .level6 = (int)values[34],
      .pan6 = (int)values[35],
      .delay6 = (int)values[36],
      .depth6 = (int)values[37],
      .rate6 = (int)values[38],
      .level7 = (int)values[39],
      .pan7 = (int)values[40],
      .delay7 = (int)values[41],
      .depth7 = (int)values[42],
      .rate7 = (int)values[43],
      .level8 = (int)values[44],
      .pan8 = (int)values[45],
      .delay8 = (int)values[46],
      .depth8 = (int)values[47],
      .rate8 = (int)values[48],
  };
}

static void chorus8_words(const ifx_chorus8_config *config, int32_t *words) {
  for (unsigned word = 0; word < IFX_CHORUS8_COEF_COUNT; word++) {
    words[word] = config->coef[word];
  }
}

/* A long slow sweep brings the voice accumulator within the filter write
 * pointer's $200 window, exercising the phase re-sync. */
static const ifx_effect_extra k_extra[] = {
    {11, 1, 64},  {16, 1, 64},  {21, 1, 64},  {26, 1, 64},  {31, 1, 64},
    {36, 1, 64},  {41, 1, 64},  {46, 1, 64},  {13, 0, 64},  {18, 0, 64},
    {23, 0, 64},  {28, 0, 64},  {33, 0, 64},  {38, 0, 64},  {43, 0, 64},
    {48, 0, 64},  {12, 80, 64}, {17, 80, 64}, {22, 80, 64}, {27, 80, 64},
    {32, 80, 64}, {37, 80, 64}, {42, 80, 64}, {47, 80, 64}, {13, 0, 65536},
};

IFX_EFFECT_DEFINE(chorus8, chorus8, k_knobs, chorus8_raw, chorus8_words, CHORUS8_SETUP_WORDS,
                  k_extra, sizeof k_extra / sizeof k_extra[0])
