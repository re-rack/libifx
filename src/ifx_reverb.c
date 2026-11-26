#include "ifx_reverb.h"

#include "ifx_dsp_math.h"
#include "ifx_mixer_core.h"
#include "ifx_mixer_design.h"
#include "ifx_tables.h"

enum {
  PREDELAY_M = 0x1FFF,
  PREDELAY_START = 0x2000,
  REVERB_M = 0x7FBA,
  BLOCK_MASK = 0x1F,
  LOWPASS_COEF = 0x028F5C,
  TAP_COEF = 0x400000,
  GATE_LIMIT = 0x7FFCB9,
  BLOCK_BASE_LEFT = 0,
  BLOCK_BASE_RIGHT = 1,
};

/* The DSP56000 AGU's modulo update, M+1 words in the next power-of-two window. */
static uint32_t step(uint32_t address, int delta, uint32_t m) {
  const uint32_t modulo = m + 1u;
  uint32_t bufsize = 1u;
  while (bufsize < modulo) {
    bufsize <<= 1u;
  }
  const uint32_t bufmask = bufsize - 1u;
  uint32_t reg = address | 0x10000u;
  const uint32_t low = reg - (reg & bufmask);
  const uint32_t high = low + modulo - 1u;
  const uint32_t magnitude = (uint32_t)(delta < 0 ? -delta : delta);
  if ((magnitude & bufmask) != 0u) {
    if (delta < 0) {
      reg -= magnitude;
    } else {
      reg += magnitude;
    }
    if (reg > high) {
      reg -= modulo;
    } else if (reg < low) {
      reg += modulo;
    }
  } else if (delta < 0) {
    reg -= magnitude;
  } else {
    reg += magnitude;
  }
  return reg & 0xFFFFu;
}

static int32_t load_word(const int32_t *memory, uint32_t address) {
  return memory[address & 0xFFFFu];
}

static int64_t load_acc(const int32_t *memory, uint32_t address) {
  return ifx_word_acc(load_word(memory, address));
}

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
    state->write = step(state->write, 1, PREDELAY_M);
  }
  state->lowpass = lowpass;
}

/* P:$00F9..$00FE: the predelay taps, panned into the two working blocks. */
static void read_predelay(const ifx_reverb_state *state, const ifx_reverb_config *config,
                          uint32_t base, int32_t left[IFX_BLOCK_FRAMES],
                          int32_t right[IFX_BLOCK_FRAMES]) {
  uint32_t r2 = step(base, -(int)config->coef[IFX_REVERB_COEF_PREDELAY_LEFT], PREDELAY_M);
  uint32_t r3 = step(base, -(int)config->coef[IFX_REVERB_COEF_PREDELAY_RIGHT], PREDELAY_M);
  const int32_t pan_left = config->coef[IFX_REVERB_COEF_PAN_LEFT];
  const int32_t pan_right = config->coef[IFX_REVERB_COEF_PAN_RIGHT];
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    left[i] = ifx_store24(ifx_mpyr(pan_left, load_word(state->memory, r2)));
    r2 = step(r2, 1, PREDELAY_M);
    right[i] = ifx_store24(ifx_mpyr(pan_right, load_word(state->memory, r3)));
    r3 = step(r3, 1, PREDELAY_M);
  }
}

/* P:$0139..$0148: four two-word allpass lines over one 32-word block. */
static void allpass_block(ifx_reverb_state *state, const int32_t block[IFX_BLOCK_FRAMES],
                          unsigned base) {
  unsigned index = base;
  unsigned sample = 0;
  for (unsigned line = 0; line < 4u; line++) {
    uint32_t r2 = (uint32_t)state->table[index] & 0xFFFFu;
    const int32_t c = ifx_s24((uint32_t)state->table[index + 1u]);
    int64_t a = load_acc(state->memory, r2);
    r2 = step(r2, 1, REVERB_M);
    int32_t y0 = block[sample];
    sample = (sample + 1u) & BLOCK_MASK;
    for (unsigned pair = 0; pair < 16u; pair++) {
      int64_t b = load_acc(state->memory, r2);
      r2 = step(r2, -1, REVERB_M);
      a = ifx_macr(a, y0, c);
      state->memory[r2] = ifx_store24(a);
      r2 = step(r2, 2, REVERB_M);
      a = load_acc(state->memory, r2);
      r2 = step(r2, -1, REVERB_M);
      const int32_t y1 = block[sample];
      sample = (sample + 1u) & BLOCK_MASK;
      b = ifx_macr(b, c, y1);
      state->memory[r2] = ifx_store24(b);
      r2 = step(r2, 2, REVERB_M);
      y0 = block[sample];
      sample = (sample + 1u) & BLOCK_MASK;
    }
    r2 = step(r2, -1, REVERB_M);
    sample = (sample - 1u) & BLOCK_MASK;
    state->table[index] = (int32_t)r2;
    index += 2u;
  }
}

/* P:$0149..$0159: four delay taps at half gain, summed into one block. */
static void tap_block(const ifx_reverb_state *state, int32_t block[IFX_BLOCK_FRAMES],
                      unsigned base) {
  uint32_t r2 = (uint32_t)state->table[base] & 0xFFFFu;
  uint32_t r3 = (uint32_t)state->table[base + 2u] & 0xFFFFu;
  uint32_t r4 = (uint32_t)state->table[base + 4u] & 0xFFFFu;
  uint32_t r5 = (uint32_t)state->table[base + 6u] & 0xFFFFu;
  unsigned sample = BLOCK_MASK;
  int32_t x0 = load_word(state->memory, r2);
  r2 = step(r2, 1, REVERB_M);
  int64_t previous = 0;
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    block[sample] = ifx_store24(previous);
    sample = (sample + 1u) & BLOCK_MASK;
    int64_t a = ifx_mpy(TAP_COEF, x0);
    x0 = load_word(state->memory, r3);
    r3 = step(r3, 1, REVERB_M);
    a = ifx_mac(a, TAP_COEF, x0);
    x0 = load_word(state->memory, r4);
    r4 = step(r4, 1, REVERB_M);
    a = ifx_mac(a, TAP_COEF, x0);
    x0 = load_word(state->memory, r5);
    r5 = step(r5, 1, REVERB_M);
    a = ifx_macr(a, TAP_COEF, x0);
    x0 = load_word(state->memory, r2);
    r2 = step(r2, 1, REVERB_M);
    previous = a;
  }
  block[sample] = ifx_store24(previous);
}

/* P:$015A..$0171: four damped allpass lines over one 32-word block. */
static void allpass_damped_block(ifx_reverb_state *state, const int32_t block[IFX_BLOCK_FRAMES],
                                 unsigned base, int32_t gain, int32_t damp) {
  unsigned index = base;
  for (unsigned line = 0; line < 4u; line++) {
    uint32_t r2 = (uint32_t)state->table[index] & 0xFFFFu;
    uint32_t r5 = (uint32_t)state->table[index + 1u] & 0xFFFFu;
    unsigned sample = 0;
    int32_t y0 = block[sample];
    sample = (sample + 1u) & BLOCK_MASK;
    r5 = step(r5, -1, REVERB_M);
    int32_t x0 = load_word(state->memory, r5);
    r5 = step(r5, 1, REVERB_M);
    int64_t a = ifx_mpy(gain, y0);
    a = ifx_mac(a, damp, x0);
    x0 = load_word(state->memory, r2);
    r2 = step(r2, 1, REVERB_M);
    a = ifx_macr(a, -x0, gain);
    y0 = block[sample];
    sample = (sample + 1u) & BLOCK_MASK;
    int64_t previous = a;
    for (unsigned i = 1u; i < IFX_BLOCK_FRAMES; i++) {
      a = ifx_mpy(gain, y0);
      state->memory[r5] = ifx_store24(previous);
      r5 = step(r5, 1, REVERB_M);
      x0 = ifx_store24(previous);
      a = ifx_mac(a, damp, x0);
      x0 = load_word(state->memory, r2);
      r2 = step(r2, 1, REVERB_M);
      a = ifx_macr(a, -x0, gain);
      y0 = block[sample];
      sample = (sample + 1u) & BLOCK_MASK;
      previous = a;
    }
    state->memory[r5] = ifx_store24(previous);
    r5 = step(r5, 1, REVERB_M);
    state->table[index] = (int32_t)r2;
    state->table[index + 1u] = (int32_t)r5;
    index += 2u;
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

  allpass_block(state, left, 0x20u);
  allpass_block(state, right, 0x28u);

  const int32_t damp = config->coef[IFX_REVERB_COEF_DAMP];
  if (ifx_es(ifx_add24(ifx_word_acc(state->gate_gain), damp))) {
    state->gate_gain = ifx_store24(ifx_sub24(ifx_word_acc(GATE_LIMIT), damp));
  }
  const int32_t gain = state->gate_gain;

  tap_block(state, left, 0x00u);
  allpass_damped_block(state, left, 0x00u, gain, damp);
  tap_block(state, right, 0x08u);
  allpass_damped_block(state, right, 0x08u, gain, damp);
  tap_block(state, left, 0x10u);
  allpass_damped_block(state, left, 0x10u, gain, damp);
  tap_block(state, right, 0x18u);
  allpass_damped_block(state, right, 0x18u, gain, damp);

  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    state->wet_left[i] = left[i];
    state->wet_right[i] = right[i];
  }
}

static int32_t gate_block(ifx_reverb_state *state, const ifx_reverb_config *config, int32_t level) {
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
  return state->gate_gain;
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
  if (state->wipe.active != 0) {
    ifx_wipe_core_call(&state->wipe, state->memory, state->wet_left, state->wet_right);
  }

  const int32_t type = config->coef[IFX_REVERB_COEF_TYPE];
  if (type != state->type) {
    state->type = type;
    if (state->wipe.init_pending == 0) {
      ifx_wipe_core_restart(&state->wipe);
    }
  }

  int32_t level = 0;
  int32_t front[IFX_BLOCK_FRAMES];
  int32_t hush[IFX_BLOCK_FRAMES];
  ifx_front_core_process(&state->front, in, front, &level);
  ifx_hush_core_process(
      &state->hush, front, hush, level, config->coef[IFX_REVERB_COEF_HUSH_THRESHOLD],
      config->coef[IFX_REVERB_COEF_HUSH_SLOPE], config->coef[IFX_REVERB_COEF_HUSH_RELEASE],
      (config->coef[IFX_REVERB_COEF_FLAGS] & IFX_MIXER_FLAG_HUSH) != 0);

  if (state->wipe.skip_pass != 0) {
    state->wipe.skip_pass = 0;
  } else if (state->wipe.active == 0) {
    if (state->wipe.init_pending != 0) {
      state->wipe.init_pending = 0;
      init_config(state, config);
    }
    gate_block(state, config, level);
    process_reverb(state, config, hush);
  }

  int32_t mixed_l[IFX_BLOCK_FRAMES];
  int32_t mixed_r[IFX_BLOCK_FRAMES];
  const int direct_on = (config->coef[IFX_REVERB_COEF_FLAGS] & IFX_MIXER_FLAG_DIRECT) != 0;
  ifx_mixer_core_process(state->wet_left, state->wet_right, hush, config->coef[IFX_REVERB_COEF_WET],
                         config->coef[IFX_REVERB_COEF_DIRECT_LEFT],
                         config->coef[IFX_REVERB_COEF_DIRECT_RIGHT], direct_on, mixed_l, mixed_r);
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    out_l[i] = ifx_mixer_core_output(mixed_l[i]);
    out_r[i] = ifx_mixer_core_output(mixed_r[i]);
  }
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
  ifx_wipe_core_init(&state->wipe);
  ifx_process_block_init(&state->block);
}

IFX_PROCESS_DEFINE(reverb, reverb_block)
