#include "ifx_reverb_net_core.h"

#include "ifx_dsp_math.h"
#include "ifx_regen_core.h"

enum {
  BLOCK_MASK = IFX_BLOCK_FRAMES - 1u,
  MAX_TAPS = 4,
  NET_M = 0x3FBF,
  NET_TAPS = 3,
  NET_TAP_GAIN = 0x555555, /* 2/3 */
  NET_INPUT_LEFT = 24,
  NET_INPUT_RIGHT = 26,
};

static const int32_t k_table[IFX_REVERB_NET_CORE_TABLE_WORDS] = {
    0x00C000, 0x00CABE, 0x00CADE, 0x00D586, 0x00D5A6, 0x00DDF4, 0x00C61B,
    0x00C5FB, 0x00DE14, 0x00E8B0, 0x00E8D0, 0x00F0CE, 0x00CFFD, 0x00CFDD,
    0x00E2FB, 0x00E2DB, 0x00F0EE, 0x00FA82, 0x00D983, 0x00D963, 0x00EC67,
    0x00EC47, 0x00F551, 0x00F531, 0x00CA7E, 0x7D70A4, 0x00C5BB, 0x71EB85,
};

static uint32_t pointer(int32_t word) { return (uint32_t)word & 0xFFFFu; }

void ifx_reverb_net_core_allpass(int32_t *table, int32_t memory[0x10000],
                                 const int32_t block[IFX_BLOCK_FRAMES], unsigned lines,
                                 uint32_t m) {
  for (unsigned line = 0; line < lines; line++) {
    int32_t *const pair = &table[2u * line];
    uint32_t r2 = pointer(pair[0]);
    const int32_t c = ifx_s24((uint32_t)pair[1]);
    int64_t a = ifx_word_acc(memory[r2]);
    r2 = ifx_agu_step(r2, 1, m);
    int32_t y0 = block[0];
    for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i += 2u) {
      a = ifx_macr(a, y0, c);
      int64_t b = ifx_word_acc(memory[r2]);
      r2 = ifx_agu_step(r2, -1, m);
      const int32_t y1 = block[i + 1u];
      memory[r2] = ifx_store24(a);
      r2 = ifx_agu_step(r2, 2, m);
      b = ifx_macr(b, c, y1);
      a = ifx_word_acc(memory[r2]);
      r2 = ifx_agu_step(r2, -1, m);
      y0 = block[(i + 2u) & BLOCK_MASK];
      memory[r2] = ifx_store24(b);
      r2 = ifx_agu_step(r2, 2, m);
    }
    pair[0] = (int32_t)ifx_agu_step(r2, -1, m);
  }
}

void ifx_reverb_net_core_taps(const int32_t *table, const int32_t memory[0x10000], unsigned taps,
                              int32_t gain, uint32_t m, int32_t block[IFX_BLOCK_FRAMES]) {
  uint32_t r[MAX_TAPS];
  for (unsigned tap = 0; tap < taps; tap++) {
    r[tap] = pointer(table[2u * tap]);
  }
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    int64_t a = ifx_mpy(gain, memory[r[0]]);
    r[0] = ifx_agu_step(r[0], 1, m);
    for (unsigned tap = 1; tap + 1u < taps; tap++) {
      a = ifx_mac(a, gain, memory[r[tap]]);
      r[tap] = ifx_agu_step(r[tap], 1, m);
    }
    a = ifx_macr(a, gain, memory[r[taps - 1u]]);
    r[taps - 1u] = ifx_agu_step(r[taps - 1u], 1, m);
    block[i] = ifx_store24(a);
  }
}

void ifx_reverb_net_core_damped(int32_t *table, int32_t memory[0x10000],
                                const int32_t block[IFX_BLOCK_FRAMES], unsigned lines, int32_t damp,
                                int32_t gain, uint32_t m) {
  for (unsigned line = 0; line < lines; line++) {
    int32_t *const pair = &table[2u * line];
    uint32_t r2 = pointer(pair[0]);
    uint32_t r5 = ifx_agu_step(pointer(pair[1]), -1, m);
    int32_t x0 = memory[r5];
    r5 = ifx_agu_step(r5, 1, m);
    int64_t a = ifx_mpy(gain, block[0]);
    a = ifx_mac(a, damp, x0);
    x0 = memory[r2];
    r2 = ifx_agu_step(r2, 1, m);
    a = ifx_macr(a, -x0, gain);
    for (unsigned i = 1u; i < IFX_BLOCK_FRAMES; i++) {
      const int32_t previous = ifx_store24(a);
      memory[r5] = previous;
      r5 = ifx_agu_step(r5, 1, m);
      a = ifx_mpy(gain, block[i]);
      a = ifx_mac(a, damp, previous);
      x0 = memory[r2];
      r2 = ifx_agu_step(r2, 1, m);
      a = ifx_macr(a, -x0, gain);
    }
    memory[r5] = ifx_store24(a);
    pair[0] = (int32_t)r2;
    pair[1] = (int32_t)ifx_agu_step(r5, 1, m);
  }
}

void ifx_reverb_net_core_state_init(ifx_reverb_net_core_state *state) {
  for (unsigned i = 0; i < IFX_REVERB_NET_CORE_TABLE_WORDS; i++) {
    state->table[i] = k_table[i];
  }
}

/* P:$00DD: pan the input and the delay returns into the two work blocks. */
static void input_mix(const int32_t memory[0x10000], const int32_t input[IFX_BLOCK_FRAMES],
                      uint32_t delay_l, uint32_t delay_r, int32_t pan_l, int32_t pan_r,
                      int32_t mix_l, int32_t mix_r, int32_t ducker_gain, int32_t ducker_mode,
                      int rev_input, int32_t work_l[IFX_BLOCK_FRAMES],
                      int32_t work_r[IFX_BLOCK_FRAMES]) {
  int32_t x1 = 0;
  int32_t y1 = 0;
  int32_t y0 = 0;
  if (rev_input != 0) {
    x1 = ifx_store24(ifx_mpyr(pan_l, mix_l));
    y1 = ifx_store24(ifx_mpyr(mix_l, pan_r));
    y0 = mix_r;
    if ((ducker_mode & 1) != 0) {
      y0 = ifx_store24(ifx_mpyr(ducker_gain, mix_r));
    }
  }
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    const int32_t x0 = input[i];
    int64_t a = ifx_mpy(x1, x0);
    int64_t b = ifx_mpy(x0, y1);
    a = ifx_macr(a, y0, memory[ifx_agu_step(delay_l, (int32_t)i, IFX_REGEN_CORE_LINE_M)]);
    b = ifx_macr(b, y0, memory[ifx_agu_step(delay_r, (int32_t)i, IFX_REGEN_CORE_LINE_M)]);
    work_l[i] = ifx_store24(a);
    work_r[i] = ifx_store24(b);
  }
}

void ifx_reverb_net_core_process(ifx_reverb_net_core_state *state, int32_t memory[0x10000],
                                 const int32_t input[IFX_BLOCK_FRAMES], uint32_t delay_l,
                                 uint32_t delay_r, int32_t pan_l, int32_t pan_r, int32_t mix_l,
                                 int32_t mix_r, int32_t ducker_gain, int32_t ducker_mode,
                                 int rev_input, int32_t damp, int32_t feedback,
                                 int32_t out_l[IFX_BLOCK_FRAMES], int32_t out_r[IFX_BLOCK_FRAMES]) {
  input_mix(memory, input, delay_l, delay_r, pan_l, pan_r, mix_l, mix_r, ducker_gain, ducker_mode,
            rev_input, out_l, out_r);
  ifx_reverb_net_core_allpass(&state->table[NET_INPUT_LEFT], memory, out_l, 1u, NET_M);
  ifx_reverb_net_core_allpass(&state->table[NET_INPUT_RIGHT], memory, out_r, 1u, NET_M);
  for (unsigned stage = 0; stage < 4u; stage++) {
    int32_t *const block = (stage & 1u) == 0u ? out_l : out_r;
    int32_t *const lines = &state->table[2u * NET_TAPS * stage];
    ifx_reverb_net_core_taps(lines, memory, NET_TAPS, NET_TAP_GAIN, NET_M, block);
    ifx_reverb_net_core_damped(lines, memory, block, NET_TAPS, damp, feedback, NET_M);
  }
}

void ifx_reverb_net_core_mix(int32_t memory[0x10000], int32_t bus[2u * IFX_BLOCK_FRAMES],
                             uint32_t *read_l, uint32_t *read_r, int32_t bus_level,
                             int32_t delay_level, int32_t reverb_level, int32_t ducker_gain,
                             int32_t ducker_mode, const int32_t reverb_l[IFX_BLOCK_FRAMES],
                             const int32_t reverb_r[IFX_BLOCK_FRAMES],
                             int32_t wet_l[IFX_BLOCK_FRAMES], int32_t wet_r[IFX_BLOCK_FRAMES]) {
  if ((ducker_mode & 1) != 0) {
    delay_level = ifx_store24(ifx_mpyr(delay_level, ducker_gain));
  }
  if ((ducker_mode & 2) != 0) {
    reverb_level = ifx_store24(ifx_mpyr(reverb_level, ducker_gain));
  }
  uint32_t r_l = *read_l;
  uint32_t r_r = *read_r;
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    int64_t a = ifx_mpy(bus_level, bus[i]);
    a = ifx_mac(a, delay_level, memory[r_l]);
    a = ifx_macr(a, reverb_level, reverb_l[i]);
    wet_l[i] = ifx_store24(a);
    bus[i] = 0;
    memory[r_l] = 0;
    r_l = ifx_agu_step(r_l, 1, IFX_REGEN_CORE_LINE_M);

    a = ifx_mpy(bus_level, bus[IFX_BLOCK_FRAMES + i]);
    a = ifx_mac(a, delay_level, memory[r_r]);
    a = ifx_macr(a, reverb_level, reverb_r[i]);
    wet_r[i] = ifx_store24(a);
    bus[IFX_BLOCK_FRAMES + i] = 0;
    memory[r_r] = 0;
    r_r = ifx_agu_step(r_r, 1, IFX_REGEN_CORE_LINE_M);
  }
  *read_l = r_l;
  *read_r = r_r;
}
