#include "ifx_delay.h"

#include "ifx_analog_core.h"
#include "ifx_dsp_math.h"
#include "ifx_mixer_core.h"
#include "ifx_mixer_design.h"

enum {
  LINE_WORDS = 0x73C0,
  LINE1_BASE = 0x1800,
  LINE2_BASE = 0x8C00,
  UNITY = 0x7FFFFF,
  TYPE_FLAGS = IFX_DELAY_FLAG_STEREO | IFX_DELAY_FLAG_PINGPONG | IFX_DELAY_FLAG_2TAP,
};

static uint32_t wrapped(int32_t index) {
  index %= LINE_WORDS;
  return (uint32_t)(index < 0 ? index + LINE_WORDS : index);
}

static int32_t damped(int32_t in, int32_t *state_word, int32_t damp) {
  const int32_t word =
      ifx_store24(ifx_round24(ifx_word_acc(in) + ifx_product(damp, *state_word - in)));
  *state_word = word;
  return word;
}

/* A ring offset advanced by `step` words; the sum stays below twice the ring. */
static uint32_t ring_step(uint32_t offset, unsigned step) {
  offset += step;
  return offset >= LINE_WORDS ? offset - LINE_WORDS : offset;
}

/* The taps go through `mpyr` with $7FFFFF, which takes an LSB off large words,
 * or with 0 while DELAY is off and MUTE TYPE mutes the output. */
static int32_t read_tap(const int32_t *memory, uint32_t address, int32_t gain) {
  return gain != 0 ? ifx_mpyr_unity(memory[address & 0xFFFFu]) : 0;
}

/* The write filter into both mono lines; their pointers advance together. */
static void write_lines(int32_t *memory, uint32_t *write1, uint32_t *write2, int32_t *damp1,
                        int32_t *damp2, const int32_t left[IFX_BLOCK_FRAMES],
                        const int32_t right[IFX_BLOCK_FRAMES], int32_t damp) {
  uint32_t w = *write1;
  int32_t state1 = *damp1;
  int32_t state2 = *damp2;
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    memory[(LINE1_BASE + w) & 0xFFFFu] = damped(left[i], &state1, damp);
    memory[(LINE2_BASE + w) & 0xFFFFu] = damped(right[i], &state2, damp);
    if (++w == LINE_WORDS) {
      w = 0;
    }
  }
  *write1 = w;
  *write2 = w;
  *damp1 = state1;
  *damp2 = state2;
}

/* The write filter into the 2-Tap pair line, its pointer counting pairs. */
static void write_pairs(int32_t *memory, uint32_t *write, int32_t *state_word,
                        const int32_t in[IFX_BLOCK_FRAMES], int32_t damp) {
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i += 2u) {
    memory[(LINE1_BASE + 2u * *write) & 0xFFFFu] = damped(in[i], state_word, damp);
    memory[(LINE1_BASE + 2u * *write + 1u) & 0xFFFFu] = damped(in[i + 1u], state_word, damp);
    *write = (*write + 1u) % LINE_WORDS;
  }
}

static void process_delay(ifx_delay_state *state, const ifx_delay_config *config,
                          const int32_t input[IFX_BLOCK_FRAMES]) {
  const int32_t flags = state->delay_flags;
  const int32_t damp = config->coef[IFX_DELAY_COEF_DAMP];
  int32_t new_left[IFX_BLOCK_FRAMES];
  int32_t new_right[IFX_BLOCK_FRAMES];

  int32_t tap_gain = UNITY;
  if ((flags & IFX_DELAY_FLAG_ON) == 0 && (flags & IFX_DELAY_FLAG_MUTE_POST) != 0) {
    tap_gain = 0;
  }
  const uint32_t base1 = wrapped((int32_t)state->write1 - config->coef[IFX_DELAY_COEF_TIME1]);
  const uint32_t base2 = wrapped((int32_t)state->write2 - config->coef[IFX_DELAY_COEF_TIME2]);
  int32_t input_gain = UNITY;
  if ((flags & IFX_DELAY_FLAG_ON) == 0 && (flags & IFX_DELAY_FLAG_MUTE_PRE) != 0) {
    input_gain = 0;
  }
  const int32_t regen1 = state->regen.left;
  const int32_t regen2 = state->regen.right;
  int32_t c1l = config->coef[IFX_DELAY_COEF_GAIN1_LEFT];
  int32_t c1r = config->coef[IFX_DELAY_COEF_GAIN1_RIGHT];
  int32_t c2l = config->coef[IFX_DELAY_COEF_GAIN2_LEFT];
  int32_t c2r = config->coef[IFX_DELAY_COEF_GAIN2_RIGHT];
  if (config->coef[IFX_DELAY_COEF_DUCKER] != 0) {
    c1l = ifx_store24(ifx_mpyr(state->ducker.gain, c1l));
    c1r = ifx_store24(ifx_mpyr(state->ducker.gain, c1r));
    c2l = ifx_store24(ifx_mpyr(state->ducker.gain, c2l));
    c2r = ifx_store24(ifx_mpyr(state->ducker.gain, c2r));
  }

  if ((flags & IFX_DELAY_FLAG_2TAP) != 0) {
    for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
      const uint32_t offset1 = 2u * ring_step(base1, i / 2u) + (i & 1u);
      const uint32_t offset2 = 2u * ring_step(base2, i / 2u) + (i & 1u);
      const int32_t old_left = read_tap(state->memory, LINE1_BASE + offset1, tap_gain);
      const int32_t old_right = read_tap(state->memory, LINE1_BASE + offset2, tap_gain);
      const int64_t a = ifx_product(input[i], input_gain) + ifx_product(regen1, old_left) +
                        ifx_product(regen2, old_right);
      new_left[i] = ifx_store24(ifx_round24(a));
      const int64_t wet_left = ifx_product(old_left, c1l) + ifx_product(c2l, old_right);
      const int64_t wet_right = ifx_product(old_left, c1r) + ifx_product(c2r, old_right);
      state->wet_left[i] = ifx_store24(ifx_round24(wet_left));
      state->wet_right[i] = ifx_store24(ifx_round24(wet_right));
    }
    write_pairs(state->memory, &state->write1, &state->damp1, new_left, damp);
    state->write2 = state->write1;
  } else if ((flags & IFX_DELAY_FLAG_PINGPONG) != 0) {
    for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
      const int32_t old_left = read_tap(state->memory, LINE1_BASE + ring_step(base1, i), tap_gain);
      const int32_t old_right = read_tap(state->memory, LINE2_BASE + ring_step(base2, i), tap_gain);
      const int64_t dry = ifx_product(input[i], input_gain);
      const int64_t a = dry + ifx_product(regen2, old_right);
      new_left[i] = ifx_store24(ifx_round24(a));
      new_right[i] = ifx_store24(ifx_round24(ifx_product(regen1, old_left)));
      const int64_t wet_left = ifx_product(old_left, c1l) + ifx_product(c2l, old_right);
      const int64_t wet_right = ifx_product(old_left, c1r) + ifx_product(c2r, old_right);
      state->wet_left[i] = ifx_store24(ifx_round24(wet_left));
      state->wet_right[i] = ifx_store24(ifx_round24(wet_right));
    }
    write_lines(state->memory, &state->write1, &state->write2, &state->damp1, &state->damp2,
                new_left, new_right, damp);
  } else {
    for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
      const int32_t old_left = read_tap(state->memory, LINE1_BASE + ring_step(base1, i), tap_gain);
      const int32_t old_right = read_tap(state->memory, LINE2_BASE + ring_step(base2, i), tap_gain);
      const int64_t dry = ifx_product(input[i], input_gain);
      const int64_t a = dry + ifx_product(regen1, old_left);
      const int64_t b = dry + ifx_product(regen2, old_right);
      new_left[i] = ifx_store24(ifx_round24(a));
      new_right[i] = ifx_store24(ifx_round24(b));
      const int64_t wet_left = ifx_product(old_left, c1l) + ifx_product(c2l, old_right);
      const int64_t wet_right = ifx_product(old_left, c1r) + ifx_product(c2r, old_right);
      state->wet_left[i] = ifx_store24(ifx_round24(wet_left));
      state->wet_right[i] = ifx_store24(ifx_round24(wet_right));
    }
    write_lines(state->memory, &state->write1, &state->write2, &state->damp1, &state->damp2,
                new_left, new_right, damp);
  }
}

static void init_config(ifx_delay_state *state) {
  state->write1 = 0;
  state->write2 = 0;
  state->damp1 = 0;
  state->damp2 = 0;
}

static void delay_block(ifx_delay_state *state, const ifx_delay_config *config,
                        const int32_t in[IFX_BLOCK_FRAMES], int32_t out_l[IFX_BLOCK_FRAMES],
                        int32_t out_r[IFX_BLOCK_FRAMES]) {
  const int32_t flags = config->coef[IFX_DELAY_COEF_FLAGS];
  ifx_wipe_core_pass(&state->wipe, state->memory, state->wet_left, state->wet_right);
  ifx_regen_core_stage(&state->regen, config->coef[IFX_DELAY_COEF_REG_LEFT],
                       config->coef[IFX_DELAY_COEF_REG_RIGHT]);
  const int32_t delay_flags = config->coef[IFX_DELAY_COEF_DELAY_FLAGS];
  if ((delay_flags & TYPE_FLAGS) != (state->delay_flags & TYPE_FLAGS)) {
    ifx_wipe_core_restart(&state->wipe);
  }
  state->delay_flags = delay_flags;

  int32_t front[IFX_BLOCK_FRAMES];
  int32_t hush[IFX_BLOCK_FRAMES];
  int32_t level = 0;
  ifx_front_core_process(&state->front, in, front, &level);
  ifx_hush_core_process(
      &state->hush, front, hush, level, config->coef[IFX_DELAY_COEF_HUSH_THRESHOLD],
      config->coef[IFX_DELAY_COEF_HUSH_SLOPE], config->coef[IFX_DELAY_COEF_HUSH_RELEASE],
      (flags & IFX_MIXER_DESIGN_FLAG_HUSH) != 0);

  const ifx_wipe_core_step step = ifx_wipe_core_next(&state->wipe);
  if (step == IFX_WIPE_CORE_INIT) {
    init_config(state);
  }
  if (step != IFX_WIPE_CORE_SKIP) {
    ifx_ducker_core_process(&state->ducker, level, config->coef[IFX_DELAY_COEF_DUCK_THRESHOLD],
                            config->coef[IFX_DELAY_COEF_DUCK_ATTEN],
                            config->coef[IFX_DELAY_COEF_DUCK_RELEASE], 0);
    if ((state->delay_flags & IFX_DELAY_FLAG_2TAP) != 0) {
      ifx_regen_core_limit(&state->regen);
    }
    process_delay(state, config, hush);
  }

  ifx_mixer_core_process(state->wet_left, state->wet_right, hush, config->coef[IFX_DELAY_COEF_WET],
                         config->coef[IFX_DELAY_COEF_DIRECT_LEFT],
                         config->coef[IFX_DELAY_COEF_DIRECT_RIGHT],
                         (flags & IFX_MIXER_DESIGN_FLAG_DIRECT) != 0, out_l, out_r);
  ifx_analog_core_process(&state->analog, in, config->direct_latch[0], config->direct_latch[1],
                          config->output_gain, out_l, out_r);
}

void ifx_delay_state_init(ifx_delay_state *state, const ifx_delay_config *config) {
  for (unsigned i = 0; i < 0x10000u; i++) {
    state->memory[i] = 0;
  }
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    state->wet_left[i] = 0;
    state->wet_right[i] = 0;
  }
  ifx_front_core_state_init(&state->front);
  ifx_hush_core_state_init(&state->hush);
  ifx_ducker_core_state_init(&state->ducker, config->coef[IFX_DELAY_COEF_DUCK_RELEASE], 0);
  ifx_regen_core_state_init(&state->regen, config->coef[IFX_DELAY_COEF_REG_LEFT],
                            config->coef[IFX_DELAY_COEF_REG_RIGHT]);
  init_config(state);
  state->delay_flags = config->coef[IFX_DELAY_COEF_DELAY_FLAGS];
  ifx_wipe_core_state_init(&state->wipe);
  ifx_analog_core_state_init(&state->analog, config->direct_latch[0], config->direct_latch[1],
                             config->output_gain);
  ifx_process_block_init(&state->block);
}

IFX_PROCESS_DEFINE(delay, delay_block)
