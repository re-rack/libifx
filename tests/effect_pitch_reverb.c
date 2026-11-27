#include "ifx_effect.h"
#include "ifx_pitch_reverb.h"
#include "ifx_pitch_reverb_setup.h"

#define PITCH_REVERB_KNOBS 34u

static const ifx_effect_knob k_knobs[PITCH_REVERB_KNOBS] = {
    {"EFFECTS LVL", 0, 58, 46, 0, 58, 0, 2},
    {"L DIR LVL", 0, 58, 46, 0, 30, 0, 3},
    {"R DIR LVL", 0, 58, 46, 0, 30, 0, 4},
    {"DIRECT HUSH", 0, 1, 0, 0, 1, 0, 5},
    {"PSHIFT LVL", 0, 58, 58, 0, 58, 0, 45},
    {"DELAY LVL", 0, 58, 58, 0, 58, 0, 7},
    {"REVERB LVL", 0, 58, 48, 0, 58, 0, 8},
    {"REGEN L", 0, 58, 0, 0, 58, 0, 97},
    {"REGEN R", 0, 58, 0, 0, 58, 0, 98},
    {"HUSH I/O", 0, 1, 1, 0, 1, 0, 9},
    {"EXP THRESH", 0, 24, 8, 0, 20, 0, 10},
    {"REL RATE", 0, 9, 5, 0, 5, 0, 11},
    {"PITCH 1", 0, 180, 60, 0, 0, 0, 46},
    {"FINE 1", 0, 40, 20, 0, 0, 0, 47},
    {"LEVEL 1", 0, 58, 54, 0, 58, 0, 63},
    {"PAN 1", 0, 100, 22, 0, 25, 0, 15},
    {"DELAY 1", 0, 209, 1, IFX_EFFECT_HOLD, 4, 0, 64},
    {"PITCH 2", 0, 180, 120, 0, 180, 0, 48},
    {"FINE 2", 0, 40, 36, 0, 40, 0, 49},
    {"LEVEL 2", 0, 58, 54, 0, 58, 0, 65},
    {"PAN 2", 0, 100, 84, 0, 75, 0, 20},
    {"DELAY 2", 0, 209, 1, IFX_EFFECT_HOLD, 8, 0, 66},
    {"LEVEL 3", 0, 58, 31, 0, 58, 0, 67},
    {"PAN 3", 0, 100, 81, 0, 50, 0, 68},
    {"DELAY 3", 0, 209, 160, IFX_EFFECT_HOLD, 209, 0, 69},
    {"DUCKER", 0, 3, 0, 0, 3, 1, 16},
    {"SENSITIVITY", 0, 24, 0, 0, 16, 0, 36},
    {"ATTENUATION", 0, 58, 58, 0, 30, 0, 37},
    {"RELEASE RATE", 0, 19, 0, 0, 8, 0, 38},
    {"REV INPUT", 0, 1, 1, 0, 1, 0, 107},
    {"DIR IN PAN", 0, 100, 50, 0, 50, 0, 56},
    {"MIX DIR/DLY", 0, 100, 50, 0, 50, 0, 39},
    {"REV DECAY", 0, 99, 66, 0, 99, 0, 42},
    {"RV HF DAMP", 0, 99, 18, 0, 20, 0, 43},
};

static void pitch_reverb_raw(const int32_t *values, ifx_pitch_reverb_setup_raw *raw) {
  *raw = (ifx_pitch_reverb_setup_raw){
      .effects_level = (int)values[0],
      .direct_left = (int)values[1],
      .direct_right = (int)values[2],
      .direct_hush = (int)values[3],
      .pitch_level = (int)values[4],
      .delay_level = (int)values[5],
      .reverb_level = (int)values[6],
      .regen_left = (int)values[7],
      .regen_right = (int)values[8],
      .hush = (int)values[9],
      .hush_threshold = (int)values[10],
      .hush_release = (int)values[11],
      .pitch1 = (int)values[12],
      .fine1 = (int)values[13],
      .level1 = (int)values[14],
      .pan1 = (int)values[15],
      .delay1 = (int)values[16],
      .pitch2 = (int)values[17],
      .fine2 = (int)values[18],
      .level2 = (int)values[19],
      .pan2 = (int)values[20],
      .delay2 = (int)values[21],
      .level3 = (int)values[22],
      .pan3 = (int)values[23],
      .delay3 = (int)values[24],
      .ducker = (int)values[25],
      .ducker_sensitivity = (int)values[26],
      .ducker_attenuation = (int)values[27],
      .ducker_release = (int)values[28],
      .rev_input = (int)values[29],
      .dir_in_pan = (int)values[30],
      .mix_dir_dly = (int)values[31],
      .reverb_decay = (int)values[32],
      .hf_damp = (int)values[33],
  };
}

/* Fast pitch jumps into the phase re-sync, the delay voice at its zero and
 * maximum, and the limiter sums at their threshold. */
static const ifx_effect_extra k_extra[] = {
    {12, 180, 64}, {17, 0, 64},   {12, 0, 64},  {17, 180, 64}, {12, 60, 64},  {17, 120, 64},
    {24, 0, 64},   {24, 209, 64}, {16, 0, 64},  {21, 0, 64},   {16, 209, 64}, {21, 209, 64},
    {16, 1, 64},   {21, 1, 64},   {24, 1, 64},  {14, 55, 64},  {15, 22, 64},  {19, 7, 64},
    {20, 75, 64},  {22, 58, 64},  {23, 0, 64},  {7, 45, 64},   {8, 45, 1024}, {14, 41, 64},
    {15, 94, 64},  {19, 50, 64},  {20, 51, 64}, {22, 33, 64},  {23, 86, 64},  {7, 50, 64},
    {8, 50, 1024},
};

IFX_EFFECT_DEFINE(pitch_reverb, pitch_reverb, k_knobs, pitch_reverb_raw, k_extra,
                  sizeof k_extra / sizeof k_extra[0])
