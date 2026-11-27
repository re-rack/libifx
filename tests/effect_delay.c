#include "ifx_delay.h"
#include "ifx_delay_alt.h"
#include "ifx_delay_alt_setup.h"
#include "ifx_delay_setup.h"
#include "ifx_effect.h"

#define DELAY_KNOBS 23u

static const ifx_effect_knob k_knobs[DELAY_KNOBS] = {
    {"DELAY LVL", 0, 58, 48, 0, 40, 0, 51},   {"L DIR LVL", 0, 58, 46, 0, 40, 0, 3},
    {"R DIR LVL", 0, 58, 46, 0, 40, 0, 4},    {"DIRECT HUSH", 0, 1, 0, 0, 1, 0, 5},
    {"HUSH I/O", 0, 1, 1, 0, 1, 0, 9},        {"EXP THRESH", 0, 24, 8, 0, 20, 0, 10},
    {"REL RATE", 0, 9, 5, 0, 5, 0, 11},       {"DELAY", 0, 1, 1, 0, 1, 0, 24},
    {"MUTE TYPE", 0, 2, 0, 0, 0, 0, 25},      {"LEVEL 1", 0, 58, 58, 0, 40, 0, 27},
    {"PAN 1", 0, 100, 0, 0, 0, 0, 28},        {"DLY TIME1", 0, 150, 75, IFX_EFFECT_HOLD, 9, 0, 29},
    {"REGEN 1", 0, 58, 46, 0, 58, 0, 30},     {"LEVEL 2", 0, 58, 58, 0, 40, 0, 31},
    {"PAN 2", 0, 100, 100, 0, 100, 0, 32},    {"DLY TIME2", 0, 150, 50, IFX_EFFECT_HOLD, 6, 0, 33},
    {"REGEN 2", 0, 58, 52, 0, 58, 0, 34},     {"D TYPE", 0, 2, 0, 0, 0, 1, 35},
    {"DL HF DAMP", 0, 99, 12, 0, 12, 0, 100}, {"DUCKER", 0, 1, 1, 0, 1, 0, 14},
    {"SENSITIVITY", 0, 24, 20, 0, 16, 0, 36}, {"ATTENUATION", 0, 58, 19, 0, 30, 0, 37},
    {"RELEASE RATE", 0, 19, 8, 0, 8, 0, 38},
};

static void delay_raw(const int32_t *values, ifx_delay_setup_raw *raw) {
  *raw = (ifx_delay_setup_raw){
      .delay_level = (int)values[0],
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

/* Hold the lines under signal, then mute the delay under each MUTE TYPE. */
static const ifx_effect_extra k_extra[] = {
    {12, 58, 0},  {16, 58, 0},  {11, 9, 0},   {15, 6, 4096}, {8, 1, 0},
    {7, 0, 6000}, {7, 1, 6000}, {8, 2, 0},    {7, 0, 6000},  {7, 1, 6000},
    {8, 0, 0},    {7, 0, 6000}, {7, 1, 6000},
};

IFX_EFFECT_DEFINE(delay, delay, k_knobs, delay_raw, k_extra, sizeof k_extra / sizeof k_extra[0])

/* The variant's hold: D TYPE wipes the lines and waits for its longest tap,
 * whose words count pairs in 2-Tap; DELAY and MUTE TYPE change what the lines
 * take in, a tap ahead of the output. */
static int32_t delay_alt_tap(const int32_t *words) {
  const int32_t tap = words[IFX_DELAY_COEF_TIME1] > words[IFX_DELAY_COEF_TIME2]
                          ? words[IFX_DELAY_COEF_TIME1]
                          : words[IFX_DELAY_COEF_TIME2];
  return (words[IFX_DELAY_COEF_DELAY_FLAGS] & IFX_DELAY_FLAG_2TAP) != 0 ? 2 * tap : tap;
}

static uint32_t delay_alt_hold(const int32_t *from, const int32_t *to) {
  const int32_t types = IFX_DELAY_FLAG_STEREO | IFX_DELAY_FLAG_PINGPONG | IFX_DELAY_FLAG_2TAP;
  const int32_t a = from[IFX_DELAY_COEF_DELAY_FLAGS];
  const int32_t b = to[IFX_DELAY_COEF_DELAY_FLAGS];
  if (((a ^ b) & types) != 0) {
    return IFX_EFFECT_WIPE_BLOCKS + ifx_effect_hold_blocks(delay_alt_tap(to));
  }
  if (a != b) {
    const int32_t from_tap = delay_alt_tap(from);
    const int32_t to_tap = delay_alt_tap(to);
    return ifx_effect_hold_blocks(from_tap > to_tap ? from_tap : to_tap);
  }
  return 0;
}

IFX_EFFECT_DEFINE_ALT(delay_alt, delay_alt, k_knobs, delay_raw, k_extra,
                      sizeof k_extra / sizeof k_extra[0], delay_alt_hold)
