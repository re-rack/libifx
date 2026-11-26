#include "ifx_reverb_net_core.h"

#include "ifx_dsp_math.h"

enum { TABLE_WORDS = IFX_REVERB_NET_TABLE_WORDS, REVERB_M = IFX_REVERB_NET_M };

static const int32_t k_table[TABLE_WORDS] = {
    0x00C000, 0x00CABE, 0x00CADE, 0x00D586, 0x00D5A6, 0x00DDF4, 0x00C61B,
    0x00C5FB, 0x00DE14, 0x00E8B0, 0x00E8D0, 0x00F0CE, 0x00CFFD, 0x00CFDD,
    0x00E2FB, 0x00E2DB, 0x00F0EE, 0x00FA82, 0x00D983, 0x00D963, 0x00EC67,
    0x00EC47, 0x00F551, 0x00F531, 0x00CA7E, 0x7D70A4, 0x00C5BB, 0x71EB85,
};

static int32_t load(const int32_t *memory, uint32_t address) { return memory[address & 0xFFFFu]; }

void ifx_reverb_net_state_init(ifx_reverb_net_state *state) {
  for (unsigned i = 0; i < TABLE_WORDS; i++) {
    state->table[i] = k_table[i];
  }
  for (unsigned i = 0; i < 32u; i++) {
    state->work_l[i] = 0;
    state->work_r[i] = 0;
  }
}

/* P:$00DD: pan the input and the delay returns into the two work blocks. */
static void input_mix(const int32_t input[32], uint32_t delay_l, uint32_t delay_r, int32_t pan_l,
                      int32_t pan_r, int32_t mix_l, int32_t mix_r, int32_t ducker_gain,
                      int ducker_delay, int rev_input, int32_t work_l[32], int32_t work_r[32],
                      const int32_t memory[0x10000]) {
  int32_t x1 = 0;
  int32_t y1 = 0;
  int32_t y0 = 0;
  if (rev_input != 0) {
    x1 = ifx_store24(ifx_mpyr(pan_l, mix_l));
    y1 = ifx_store24(ifx_mpyr(mix_l, pan_r));
    y0 = mix_r;
    if (ducker_delay != 0) {
      y0 = ifx_store24(ifx_mpyr(ducker_gain, mix_r));
    }
  }
  for (unsigned i = 0; i < 32u; i++) {
    const int32_t x0 = input[i];
    int64_t a = ifx_mpy(x1, x0);
    int64_t b = ifx_mpy(x0, y1);
    const int32_t dl = load(memory, ifx_agu_step(delay_l, (int32_t)i, 0x3FFFu));
    a = ifx_macr(a, y0, dl);
    const int32_t dr = load(memory, ifx_agu_step(delay_r, (int32_t)i, 0x3FFFu));
    b = ifx_macr(b, y0, dr);
    work_l[i] = ifx_store24(a);
    work_r[i] = ifx_store24(b);
  }
}

/* P:$00E6: one two-word allpass line over a 32-word block. */
static void allpass2(ifx_reverb_net_state *state, int32_t memory[0x10000], const int32_t block[32],
                     unsigned index) {
  uint32_t r2 = (uint32_t)state->table[index] & 0xFFFFu;
  const int32_t c = ifx_s24((uint32_t)state->table[index + 1u]);
  int64_t a = ifx_word_acc(load(memory, r2));
  r2 = ifx_agu_step(r2, 1, REVERB_M);
  unsigned sample = 0;
  int32_t y0 = block[sample++];
  for (unsigned pair = 0; pair < 16u; pair++) {
    a = ifx_macr(a, y0, c);
    int64_t b = ifx_word_acc(load(memory, r2));
    r2 = ifx_agu_step(r2, -1, REVERB_M);
    const int32_t y1 = block[sample++];
    memory[r2] = ifx_store24(a);
    r2 = ifx_agu_step(r2, 2, REVERB_M);
    b = ifx_macr(b, c, y1);
    a = ifx_word_acc(load(memory, r2));
    r2 = ifx_agu_step(r2, -1, REVERB_M);
    y0 = block[sample++];
    memory[r2] = ifx_store24(b);
    r2 = ifx_agu_step(r2, 2, REVERB_M);
  }
  r2 = ifx_agu_step(r2, -1, REVERB_M);
  state->table[index] = (int32_t)r2;
}

/* P:$00F2: three taps at 2/3 summed into the block. */
static void tap_block(const ifx_reverb_net_state *state, const int32_t memory[0x10000],
                      int32_t block[32], unsigned index) {
  uint32_t r2 = (uint32_t)state->table[index] & 0xFFFFu;
  uint32_t r3 = (uint32_t)state->table[index + 2u] & 0xFFFFu;
  uint32_t r4 = (uint32_t)state->table[index + 4u] & 0xFFFFu;
  const int32_t gain = 0x555555;
  for (unsigned i = 0; i < 32u; i++) {
    int64_t a = ifx_mpy(gain, load(memory, r2));
    r2 = ifx_agu_step(r2, 1, REVERB_M);
    a = ifx_mac(a, gain, load(memory, r3));
    r3 = ifx_agu_step(r3, 1, REVERB_M);
    a = ifx_macr(a, gain, load(memory, r4));
    r4 = ifx_agu_step(r4, 1, REVERB_M);
    block[i] = ifx_store24(a);
  }
}

/* P:$0102: three damped allpass lines over the block. */
static void allpass_damped(ifx_reverb_net_state *state, int32_t memory[0x10000],
                           const int32_t block[32], unsigned index, int32_t damp, int32_t gain) {
  for (unsigned line = 0; line < 3u; line++) {
    uint32_t r2 = (uint32_t)state->table[index] & 0xFFFFu;
    uint32_t r5 = (uint32_t)state->table[index + 1u] & 0xFFFFu;
    r5 = ifx_agu_step(r5, -1, REVERB_M);
    int32_t x0 = load(memory, r5);
    r5 = ifx_agu_step(r5, 1, REVERB_M);
    int32_t y0 = block[0];
    int64_t a = ifx_mpy(gain, y0);
    a = ifx_mac(a, damp, x0);
    x0 = load(memory, r2);
    r2 = ifx_agu_step(r2, 1, REVERB_M);
    a = ifx_macr(a, -x0, gain);
    y0 = block[1];
    int64_t previous = a;
    for (unsigned i = 1u; i < 32u; i++) {
      a = ifx_mpy(gain, y0);
      memory[r5] = ifx_store24(previous);
      r5 = ifx_agu_step(r5, 1, REVERB_M);
      x0 = ifx_store24(previous);
      a = ifx_mac(a, damp, x0);
      x0 = load(memory, r2);
      r2 = ifx_agu_step(r2, 1, REVERB_M);
      a = ifx_macr(a, -x0, gain);
      y0 = block[(i + 1u) & 0x1Fu];
      previous = a;
    }
    memory[r5] = ifx_store24(previous);
    r5 = ifx_agu_step(r5, 1, REVERB_M);
    state->table[index] = (int32_t)r2;
    state->table[index + 1u] = (int32_t)r5;
    index += 2u;
  }
}

void ifx_reverb_net_process(ifx_reverb_net_state *state, int32_t memory[0x10000],
                            const int32_t input[32], uint32_t delay_l, uint32_t delay_r,
                            int32_t pan_l, int32_t pan_r, int32_t mix_l, int32_t mix_r,
                            int32_t ducker_gain, int ducker_delay, int rev_input, int32_t damp,
                            int32_t feedback, int32_t out_l[32], int32_t out_r[32]) {
  input_mix(input, delay_l, delay_r, pan_l, pan_r, mix_l, mix_r, ducker_gain, ducker_delay,
            rev_input, state->work_l, state->work_r, memory);

  allpass2(state, memory, state->work_l, 24u);
  allpass2(state, memory, state->work_r, 26u);

  for (unsigned stage = 0; stage < 4u; stage++) {
    int32_t *const block = (stage & 1u) == 0u ? state->work_l : state->work_r;
    const unsigned base = stage * 6u;
    tap_block(state, memory, block, base);
    allpass_damped(state, memory, block, base, damp, feedback);
  }

  for (unsigned i = 0; i < 32u; i++) {
    out_l[i] = state->work_l[i];
    out_r[i] = state->work_r[i];
  }
}
