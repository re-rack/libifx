#include "ifx_effect.h"
#include "ifx_reverb.h"
#include "ifx_reverb_alt.h"
#include "ifx_reverb_alt_setup.h"
#include "ifx_reverb_setup.h"

#define REVERB_KNOBS 18u
/* PREDELAY L/R ring: 8,192 frames, shorter than IFX_EFFECT_HOLD. */
#define REVERB_HOLD 8192u

static const ifx_effect_knob k_knobs[REVERB_KNOBS] = {
    {"REVERB LVL", 0, 58, 46, 0, 58, 0, 50},
    {"L DIR LVL", 0, 58, 46, 0, 46, 0, 3},
    {"R DIR LVL", 0, 58, 46, 0, 46, 0, 4},
    {"DIRECT HUSH", 0, 1, 0, 0, 1, 0, 5},
    {"HUSH I/O", 0, 1, 1, 0, 1, 0, 9},
    {"EXP THRESH", 0, 24, 8, 0, 20, 0, 10},
    {"REL RATE", 0, 9, 5, 0, 5, 0, 11},
    {"REVERB DECAY", 0, 99, 81, 0, 99, 0, 42},
    {"RV HF DAMP", 0, 99, 42, 0, 20, 0, 43},
    {"LOW FREQ", 0, 99, 50, 0, 50, 0, 54},
    {"REV TYPE", 0, 7, 6, 0, 6, 1, 55},
    {"DIRIN PAN", 0, 100, 49, 0, 50, 0, 56},
    {"PREDELAY L", 0, 209, 157, REVERB_HOLD, 20, 0, 57},
    {"PREDELAY R", 0, 209, 209, REVERB_HOLD, 30, 0, 58},
    {"GATE", 0, 1, 0, 0, 1, 0, 59},
    {"GATE DECAY", 0, 31, 0, 0, 4, 0, 60},
    {"GATE THRESH", 0, 24, 0, 0, 24, 0, 61},
    {"HOLD TIME", 0, 99, 0, 0, 1, 0, 62},
};

static void reverb_raw(const int32_t *values, ifx_reverb_setup_raw *raw) {
  *raw = (ifx_reverb_setup_raw){
      .reverb_level = (int)values[0],
      .direct_left = (int)values[1],
      .direct_right = (int)values[2],
      .direct_hush = (int)values[3],
      .hush = (int)values[4],
      .hush_threshold = (int)values[5],
      .hush_release = (int)values[6],
      .reverb_decay = (int)values[7],
      .hf_damp = (int)values[8],
      .low_freq = (int)values[9],
      .reverb_type = (int)values[10],
      .dir_in_pan = (int)values[11],
      .predelay_left = (int)values[12],
      .predelay_right = (int)values[13],
      .gate = (int)values[14],
      .gate_decay = (int)values[15],
      .gate_threshold = (int)values[16],
      .gate_hold = (int)values[17],
  };
}

/* Raise the damping while the gate holds a low-damped feedback word so
 * Y:$000B + Y:$0001 overflows. */
static const ifx_effect_extra k_extra[] = {
    {7, 99, 0}, {8, 0, 0},  {14, 1, 0},    {16, 24, 0},
    {15, 4, 0}, {17, 0, 0}, {7, 99, 7491}, {8, 33, 2048},
};

IFX_EFFECT_DEFINE(reverb, reverb, k_knobs, reverb_raw, k_extra, sizeof k_extra / sizeof k_extra[0])

/* The variant's hold: REV TYPE wipes the memory and waits for the longer
 * predelay. */
static uint32_t reverb_alt_hold(const int32_t *from, const int32_t *to) {
  if (from[IFX_REVERB_COEF_TYPE] == to[IFX_REVERB_COEF_TYPE]) {
    return 0;
  }
  const int32_t left = to[IFX_REVERB_COEF_PREDELAY_LEFT];
  const int32_t right = to[IFX_REVERB_COEF_PREDELAY_RIGHT];
  return IFX_EFFECT_WIPE_BLOCKS + ifx_effect_hold_blocks(left > right ? left : right);
}

IFX_EFFECT_DEFINE_ALT(reverb_alt, reverb_alt, k_knobs, reverb_raw, k_extra,
                      sizeof k_extra / sizeof k_extra[0], reverb_alt_hold)
