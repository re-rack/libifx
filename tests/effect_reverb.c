#include "ifx_effect.h"
#include "ifx_reverb.h"
#include "ifx_reverb_setup.h"

#define REVERB_KNOBS 18u
#define REVERB_SETUP_WORDS IFX_REVERB_COEF_COUNT
#define REVERB_HOLD 8192u

static const ifx_effect_knob k_knobs[REVERB_KNOBS] = {
    {"REVERB LVL", 0, 58, 46, 0, 58, 0},
    {"L DIR LVL", 0, 58, 46, 0, 46, 0},
    {"R DIR LVL", 0, 58, 46, 0, 46, 0},
    {"DIRECT HUSH", 0, 1, 0, 0, 1, 0},
    {"HUSH I/O", 0, 1, 1, 0, 1, 0},
    {"EXP THRESH", 0, 24, 8, 0, 20, 0},
    {"REL RATE", 0, 9, 5, 0, 5, 0},
    {"REVERB DECAY", 0, 99, 81, 0, 99, 0},
    {"RV HF DAMP", 0, 99, 42, 0, 20, 0},
    {"LOW FREQ", 0, 99, 50, 0, 50, 0},
    {"REV TYPE", 0, 7, 6, 0, 6, 1},
    {"DIRIN PAN", 0, 100, 49, 0, 50, 0},
    {"PREDELAY L", 0, 209, 157, REVERB_HOLD, 20, 0},
    {"PREDELAY R", 0, 209, 209, REVERB_HOLD, 30, 0},
    {"GATE", 0, 1, 0, 0, 1, 0},
    {"GATE DECAY", 0, 31, 0, 0, 4, 0},
    {"GATE THRESH", 0, 24, 0, 0, 24, 0},
    {"HOLD TIME", 0, 99, 0, 0, 1, 0},
};

static void reverb_raw(const int32_t *values, ifx_reverb_setup_raw *raw) {
  *raw = (ifx_reverb_setup_raw){
      .effect_level = (int)values[0],
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

static void reverb_words(const ifx_reverb_config *config, int32_t *words) {
  for (unsigned word = 0; word < IFX_REVERB_COEF_COUNT; word++) {
    words[word] = config->coef[word];
  }
}

/* The long quiet stretch stages a low-damped feedback word, then raises the
 * damping while the gate holds so Y:$000B + Y:$0001 overflows. */
static const ifx_effect_extra k_extra[] = {
    {7, 99, 0},   /* REVERB DECAY */
    {8, 0, 0},    /* RV HF DAMP */
    {14, 1, 0},   /* GATE */
    {16, 24, 0},  /* GATE THRESH */
    {15, 4, 0},   /* GATE DECAY */
    {17, 0, 0},   /* HOLD TIME */
    {7, 99, 7491}, {8, 33, 2048},
};

IFX_EFFECT_DEFINE(reverb, reverb, k_knobs, reverb_raw, reverb_words, REVERB_SETUP_WORDS, k_extra,
                  sizeof k_extra / sizeof k_extra[0])
