#include "ifx_reverb.h"

#include "ifx_analog_core.h"
#include "ifx_dsp_math.h"
#include "ifx_mixer_core.h"
#include "ifx_mixer_design.h"
#include "ifx_reverb_net_core.h"

enum {
  PREDELAY_M = 0x1FFF,
  PREDELAY_START = 0x2000,
  REVERB_M = 0x7FBA,
  LOWPASS_COEF = 0x028F5C,
  LINES = 4,
  TAP_GAIN = 0x400000, /* 0.5 */
  GATE_LIMIT = 0x7FFCB9,
  INPUT_LEFT = 0x20,
  INPUT_RIGHT = 0x28,
};

/* P:$00D6..$00F8: the delay memory write filter; LOW FREQ adds the filtered
 * low band to the input, and the predelay ring advances one word a sample. */
static void write_predelay(ifx_reverb_state *state, const ifx_reverb_config *config,
                           const int32_t in[IFX_BLOCK_FRAMES]) {
  const int32_t low_freq = config->coef[IFX_REVERB_COEF_LOW_FREQ];
  int32_t lowpass = state->lowpass;
  /* The accumulator keeps its low word from sample to sample; only A1 is stored. */
  int64_t a = ifx_word_acc(lowpass);
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    a = ifx_mac(a, -lowpass, LOWPASS_COEF);
    int64_t b = ifx_word_acc(in[i]);
    a = ifx_mac(a, LOWPASS_COEF, in[i]);
    lowpass = ifx_store24(a);
    b = ifx_macr(b, lowpass, low_freq);
    state->memory[state->write] = ifx_store24(b);
    state->write = ifx_agu_step(state->write, 1, PREDELAY_M);
  }
  state->lowpass = lowpass;
}

/* P:$00F9..$00FE: the predelay taps, panned into the two working blocks. */
static void read_predelay(const ifx_reverb_state *state, const ifx_reverb_config *config,
                          uint32_t base, int32_t left[IFX_BLOCK_FRAMES],
                          int32_t right[IFX_BLOCK_FRAMES]) {
  uint32_t r2 = ifx_agu_step(base, -(int)config->coef[IFX_REVERB_COEF_PREDELAY_LEFT], PREDELAY_M);
  uint32_t r3 = ifx_agu_step(base, -(int)config->coef[IFX_REVERB_COEF_PREDELAY_RIGHT], PREDELAY_M);
  const int32_t pan_left = config->coef[IFX_REVERB_COEF_PAN_LEFT];
  const int32_t pan_right = config->coef[IFX_REVERB_COEF_PAN_RIGHT];
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    left[i] = ifx_store24(ifx_mpyr(pan_left, state->memory[r2]));
    r2 = ifx_agu_step(r2, 1, PREDELAY_M);
    right[i] = ifx_store24(ifx_mpyr(pan_right, state->memory[r3]));
    r3 = ifx_agu_step(r3, 1, PREDELAY_M);
  }
}

/* P:$00D6..$0138: the configuration's reverb path, Hush block to wet block. */
static void process_reverb(ifx_reverb_state *state, const ifx_reverb_config *config,
                           const int32_t hush[IFX_BLOCK_FRAMES]) {
  int32_t left[IFX_BLOCK_FRAMES];
  int32_t right[IFX_BLOCK_FRAMES];
  const uint32_t base = state->write;
  write_predelay(state, config, hush);
  read_predelay(state, config, base, left, right);

  ifx_reverb_net_core_allpass(&state->table[INPUT_LEFT], state->memory, left, LINES, REVERB_M);
  ifx_reverb_net_core_allpass(&state->table[INPUT_RIGHT], state->memory, right, LINES, REVERB_M);

  const int32_t damp = config->coef[IFX_REVERB_COEF_DAMP];
  if (ifx_es(ifx_add24(ifx_word_acc(state->gate_gain), damp))) {
    state->gate_gain = ifx_store24(ifx_sub24(ifx_word_acc(GATE_LIMIT), damp));
  }
  for (unsigned stage = 0; stage < 4u; stage++) {
    int32_t *const block = (stage & 1u) == 0u ? left : right;
    int32_t *const lines = &state->table[2u * LINES * stage];
    ifx_reverb_net_core_taps(lines, state->memory, LINES, TAP_GAIN, REVERB_M, block);
    ifx_reverb_net_core_damped(lines, state->memory, block, LINES, damp, state->gate_gain,
                               REVERB_M);
  }

  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    state->wet_left[i] = left[i];
    state->wet_right[i] = right[i];
  }
}

/* P:$00B7..$00D5: the gate keeps the feedback word while the block level
 * reaches GATE THRESH and for HOLD TIME blocks after, then decays it a block
 * at a time. */
static void gate_block(ifx_reverb_state *state, const ifx_reverb_config *config, int32_t level) {
  if ((config->coef[IFX_REVERB_COEF_FLAGS] & IFX_REVERB_FLAG_GATE) == 0) {
    state->gate_gain = config->coef[IFX_REVERB_COEF_FEEDBACK];
  } else if (ifx_abs56(ifx_word_acc(config->coef[IFX_REVERB_COEF_GATE_THRESHOLD])) >
             ifx_abs56(ifx_word_acc(level))) {
    if (state->gate_hold != 0) {
      state->gate_hold--;
    } else {
      state->gate_gain =
          ifx_store24(ifx_mpyr(state->gate_gain, config->coef[IFX_REVERB_COEF_GATE_DECAY]));
    }
  } else {
    state->gate_gain = config->coef[IFX_REVERB_COEF_FEEDBACK];
    state->gate_hold = config->coef[IFX_REVERB_COEF_GATE_HOLD];
  }
}

static void init_config(ifx_reverb_state *state, const ifx_reverb_config *config) {
  state->gate_gain = 0;
  state->gate_hold = 0;
  state->lowpass = 0;
  state->write = PREDELAY_START;
  const unsigned type = (unsigned)config->coef[IFX_REVERB_COEF_TYPE] & 7u;
  for (unsigned i = 0; i < IFX_REVERB_TYPE_TABLE_WORDS; i++) {
    state->table[i] = ifx_reverb_type_table[type][i];
  }
}

static void reverb_block(ifx_reverb_state *state, const ifx_reverb_config *config,
                         const int32_t in[IFX_BLOCK_FRAMES], int32_t out_l[IFX_BLOCK_FRAMES],
                         int32_t out_r[IFX_BLOCK_FRAMES]) {
  const int32_t flags = config->coef[IFX_REVERB_COEF_FLAGS];
  ifx_wipe_core_pass(&state->wipe, state->memory, state->wet_left, state->wet_right);
  if (config->coef[IFX_REVERB_COEF_TYPE] != state->type) {
    state->type = config->coef[IFX_REVERB_COEF_TYPE];
    ifx_wipe_core_restart(&state->wipe);
  }

  int32_t front[IFX_BLOCK_FRAMES];
  int32_t hush[IFX_BLOCK_FRAMES];
  int32_t level = 0;
  ifx_front_core_process(&state->front, in, front, &level);
  ifx_hush_core_process(
      &state->hush, front, hush, level, config->coef[IFX_REVERB_COEF_HUSH_THRESHOLD],
      config->coef[IFX_REVERB_COEF_HUSH_SLOPE], config->coef[IFX_REVERB_COEF_HUSH_RELEASE],
      (flags & IFX_MIXER_DESIGN_FLAG_HUSH) != 0);

  const ifx_wipe_core_step step = ifx_wipe_core_next(&state->wipe);
  if (step == IFX_WIPE_CORE_INIT) {
    init_config(state, config);
  }
  if (step != IFX_WIPE_CORE_SKIP) {
    gate_block(state, config, level);
    process_reverb(state, config, hush);
  }

  ifx_mixer_core_process(state->wet_left, state->wet_right, hush, config->coef[IFX_REVERB_COEF_WET],
                         config->coef[IFX_REVERB_COEF_DIRECT_LEFT],
                         config->coef[IFX_REVERB_COEF_DIRECT_RIGHT],
                         (flags & IFX_MIXER_DESIGN_FLAG_DIRECT) != 0, out_l, out_r);
  ifx_analog_core_process(&state->analog, in, config->direct_latch[0], config->direct_latch[1],
                          config->output_gain, out_l, out_r);
}

void ifx_reverb_state_init(ifx_reverb_state *state, const ifx_reverb_config *config) {
  for (unsigned i = 0; i < 0x10000u; i++) {
    state->memory[i] = 0;
  }
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    state->wet_left[i] = 0;
    state->wet_right[i] = 0;
  }
  ifx_front_core_state_init(&state->front);
  ifx_hush_core_state_init(&state->hush);
  state->type = config->coef[IFX_REVERB_COEF_TYPE];
  init_config(state, config);
  ifx_wipe_core_state_init(&state->wipe);
  ifx_analog_core_state_init(&state->analog, config->direct_latch[0], config->direct_latch[1],
                             config->output_gain);
  ifx_process_block_init(&state->block);
}

IFX_PROCESS_DEFINE(reverb, reverb_block)
