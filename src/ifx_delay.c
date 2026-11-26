#include "ifx_delay.h"

#include "ifx_dsp_math.h"
#include "ifx_ducker_core.h"
#include "ifx_mixer_core.h"
#include "ifx_mixer_design.h"
#include "ifx_regen_core.h"
#include "ifx_wipe_core.h"

enum {
  LINE_WORDS = (int)IFX_DELAY_LINE_WORDS,
  LINE1_BASE = 0x1800,
  LINE2_BASE = 0x8C00,
};

static uint32_t wrapped(int index) {
  index %= LINE_WORDS;
  return (uint32_t)(index < 0 ? index + LINE_WORDS : index);
}

static void read_lines(const ifx_delay_state *state, const ifx_delay_config *config,
                       int32_t left[32], int32_t right[32]) {
  const int32_t flags = state->delay_flags;
  int32_t gain = 0x7FFFFF;
  if ((flags & IFX_DELAY_ON) == 0 && (flags & IFX_DELAY_MUTE_POST) != 0) {
    gain = 0;
  }
  const uint32_t base1 = wrapped((int)state->write1 - config->coef[IFX_DELAY_COEF_TIME1]);
  const uint32_t base2 = wrapped((int)state->write2 - config->coef[IFX_DELAY_COEF_TIME2]);
  for (unsigned i = 0; i < 32u; i++) {
    uint32_t address1;
    uint32_t address2;
    if ((flags & IFX_DELAY_TYPE_2TAP) != 0) {
      address1 = LINE1_BASE + 2u * ((base1 + i / 2u) % IFX_DELAY_LINE_WORDS) + (i & 1u);
      address2 = LINE1_BASE + 2u * ((base2 + i / 2u) % IFX_DELAY_LINE_WORDS) + (i & 1u);
    } else {
      address1 = LINE1_BASE + (base1 + i) % IFX_DELAY_LINE_WORDS;
      address2 = LINE2_BASE + (base2 + i) % IFX_DELAY_LINE_WORDS;
    }
    left[i] = ifx_store24(ifx_mpyr(state->memory[address1 & 0xFFFFu], gain));
    right[i] = ifx_store24(ifx_mpyr(state->memory[address2 & 0xFFFFu], gain));
  }
}

static void filter_mono(int32_t *memory, uint32_t base, uint32_t *write, int32_t *state_word,
                        const int32_t scratch[32], int32_t damp) {
  int32_t x1 = *state_word;
  for (unsigned i = 0; i < 32u; i++) {
    const int32_t x0 = scratch[i];
    int64_t a = ifx_word_acc(scratch[i]);
    a = ifx_mac(a, damp, x1);
    a = ifx_macr(a, -x0, damp);
    const int32_t out = ifx_store24(a);
    x1 = out;
    memory[(base + *write) & 0xFFFFu] = out;
    *write = (*write + 1u) % IFX_DELAY_LINE_WORDS;
  }
  *state_word = x1;
}

static void filter_pairs(int32_t *memory, uint32_t *write, int32_t *state_word,
                         const int32_t scratch[32], int32_t damp) {
  int32_t x1 = *state_word;
  for (unsigned pair = 0; pair < 16u; pair++) {
    const unsigned first = 2u * pair;
    int32_t x0 = scratch[first];
    int64_t a = ifx_word_acc(scratch[first]);
    a = ifx_mac(a, damp, x1);
    a = ifx_macr(a, -x0, damp);
    const int32_t out0 = ifx_store24(a);
    x1 = out0;
    memory[(LINE1_BASE + 2u * *write) & 0xFFFFu] = out0;

    x0 = scratch[first + 1u];
    a = ifx_word_acc(scratch[first + 1u]);
    a = ifx_mac(a, damp, x1);
    a = ifx_macr(a, -x0, damp);
    const int32_t out1 = ifx_store24(a);
    x1 = out1;
    memory[(LINE1_BASE + 2u * *write + 1u) & 0xFFFFu] = out1;
    *write = (*write + 1u) % IFX_DELAY_LINE_WORDS;
  }
  *state_word = x1;
}

static void process_delay(ifx_delay_state *state, const ifx_delay_config *config,
                          const int32_t input[32], int32_t left[32], int32_t right[32]) {
  const int32_t flags = state->delay_flags;
  int32_t old_left[32];
  int32_t old_right[32];
  int32_t new_left[32];
  int32_t new_right[32];
  read_lines(state, config, old_left, old_right);

  int32_t input_gain = 0x7FFFFF;
  if ((flags & IFX_DELAY_ON) == 0 && (flags & IFX_DELAY_MUTE_PRE) != 0) {
    input_gain = 0;
  }
  const int32_t regen1 = state->regen1;
  const int32_t regen2 = state->regen2;
  if ((flags & IFX_DELAY_TYPE_2TAP) != 0) {
    for (unsigned i = 0; i < 32u; i++) {
      int64_t a = ifx_mpy(input[i], input_gain);
      a = ifx_mac(a, regen1, old_left[i]);
      a = ifx_macr(a, regen2, old_right[i]);
      new_left[i] = ifx_store24(a);
    }
    filter_pairs(state->memory, &state->write1, &state->damp1, new_left,
                 config->coef[IFX_DELAY_COEF_DAMP]);
    state->write2 = state->write1;
  } else {
    if ((flags & IFX_DELAY_TYPE_PINGPONG) != 0) {
      for (unsigned i = 0; i < 32u; i++) {
        int64_t a = ifx_mpy(input[i], input_gain);
        a = ifx_macr(a, regen2, old_right[i]);
        const int64_t b = ifx_mpyr(regen1, old_left[i]);
        new_left[i] = ifx_store24(a);
        new_right[i] = ifx_store24(b);
      }
    } else {
      for (unsigned i = 0; i < 32u; i++) {
        int64_t a = ifx_mpy(input[i], input_gain);
        int64_t b = ifx_mpy(input[i], input_gain);
        a = ifx_macr(a, regen1, old_left[i]);
        b = ifx_macr(b, regen2, old_right[i]);
        new_left[i] = ifx_store24(a);
        new_right[i] = ifx_store24(b);
      }
    }
    filter_mono(state->memory, LINE1_BASE, &state->write1, &state->damp1, new_left,
                config->coef[IFX_DELAY_COEF_DAMP]);
    filter_mono(state->memory, LINE2_BASE, &state->write2, &state->damp2, new_right,
                config->coef[IFX_DELAY_COEF_DAMP]);
  }

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
  for (unsigned i = 0; i < 32u; i++) {
    int64_t a = ifx_mpy(old_left[i], c1l);
    a = ifx_macr(a, c2l, old_right[i]);
    left[i] = ifx_store24(a);
    a = ifx_mpy(old_left[i], c1r);
    a = ifx_macr(a, c2r, old_right[i]);
    right[i] = ifx_store24(a);
  }
}

static void delay_block(ifx_delay_state *state, const ifx_delay_config *config,
                        const int32_t in[IFX_BLOCK_FRAMES], int32_t out_l[IFX_BLOCK_FRAMES],
                        int32_t out_r[IFX_BLOCK_FRAMES]) {
  if (state->wipe.active != 0) {
    ifx_wipe_core_call(&state->wipe, state->memory, state->wet_left, state->wet_right);
  }

  if (state->staged_regen1 != config->coef[IFX_DELAY_COEF_REG_LEFT]) {
    state->staged_regen1 = config->coef[IFX_DELAY_COEF_REG_LEFT];
    state->regen1 = state->staged_regen1;
  }
  if (state->staged_regen2 != config->coef[IFX_DELAY_COEF_REG_RIGHT]) {
    state->staged_regen2 = config->coef[IFX_DELAY_COEF_REG_RIGHT];
    state->regen2 = state->staged_regen2;
  }

  const int32_t flags = config->coef[IFX_DELAY_COEF_DELAY_FLAGS];
  const int32_t type_mask = IFX_DELAY_TYPE_STEREO | IFX_DELAY_TYPE_PINGPONG | IFX_DELAY_TYPE_2TAP;
  if ((flags & type_mask) != (state->delay_flags & type_mask)) {
    if (state->wipe.init_pending == 0) {
      ifx_wipe_core_restart(&state->wipe);
    }
  }
  state->delay_flags = flags;

  int32_t level = 0;
  int32_t front[32];
  int32_t hush[32];
  ifx_front_core_process(&state->front, in, front, &level);
  ifx_hush_core_process(
      &state->hush, front, hush, level, config->coef[IFX_DELAY_COEF_HUSH_THRESHOLD],
      config->coef[IFX_DELAY_COEF_HUSH_SLOPE], config->coef[IFX_DELAY_COEF_HUSH_RELEASE],
      (config->coef[IFX_DELAY_COEF_FLAGS] & IFX_MIXER_FLAG_HUSH) != 0);
  if (state->wipe.skip_pass != 0) {
    state->wipe.skip_pass = 0;
  } else if (state->wipe.active == 0) {
    if (state->wipe.init_pending != 0) {
      state->wipe.init_pending = 0;
      state->write1 = 0;
      state->write2 = 0;
      state->damp1 = 0;
      state->damp2 = 0;
    }
    ifx_ducker_core_process(&state->ducker, level, config->coef[IFX_DELAY_COEF_DUCK_THRESHOLD],
                            config->coef[IFX_DELAY_COEF_DUCK_ATTEN],
                            config->coef[IFX_DELAY_COEF_DUCK_RELEASE]);
    if ((state->delay_flags & IFX_DELAY_TYPE_2TAP) != 0) {
      ifx_regen_core_limit(&state->regen1, &state->regen2);
    }
    process_delay(state, config, hush, state->wet_left, state->wet_right);
  }

  int32_t mixed_l[32];
  int32_t mixed_r[32];
  const int direct_on = (config->coef[IFX_DELAY_COEF_FLAGS] & IFX_MIXER_FLAG_DIRECT) != 0;
  ifx_mixer_core_process(state->wet_left, state->wet_right, hush, config->coef[IFX_DELAY_COEF_WET],
                         config->coef[IFX_DELAY_COEF_DIRECT_LEFT],
                         config->coef[IFX_DELAY_COEF_DIRECT_RIGHT], direct_on, mixed_l, mixed_r);
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    out_l[i] = ifx_mixer_core_output(mixed_l[i]);
    out_r[i] = ifx_mixer_core_output(mixed_r[i]);
  }
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
  ifx_ducker_core_state_init(&state->ducker);
  state->write1 = 0;
  state->write2 = 0;
  state->damp1 = 0;
  state->damp2 = 0;
  state->staged_regen1 = config->coef[IFX_DELAY_COEF_REG_LEFT];
  state->staged_regen2 = config->coef[IFX_DELAY_COEF_REG_RIGHT];
  state->regen1 = state->staged_regen1;
  state->regen2 = state->staged_regen2;
  state->delay_flags = config->coef[IFX_DELAY_COEF_DELAY_FLAGS];
  ifx_wipe_core_init(&state->wipe);
  ifx_process_block_init(&state->block);
}

IFX_PROCESS_DEFINE(delay, delay_block)
