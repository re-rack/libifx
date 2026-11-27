#include "ifx_chorus_core.h"

#include <stddef.h>

#include "ifx_dsp_math.h"

enum {
  HISTORY_BASE = 0x40,
  HISTORY_M = IFX_REGEN_CORE_HISTORY_WORDS - 1u,
  FILTER6_M = 0x1FFF,
  SWEEP_WINDOW = 0x2000, /* the voices read the filter history at $2000 + phase */
  LFO_M = 0x1F,
  LINE_L = 0x4000,
  LINE_R = 0x8000,
};

static const int32_t k_tap16[16] = {
    -0xC30C,  0x1A200,   -0x2FE65, 0x51074,  -0x84741, 0xDBEE3,  -0x19916F, 0x50F1EB,
    0x50F1EB, -0x19916F, 0xDBEE3,  -0x84741, 0x51074,  -0x2FE65, 0x1A200,   -0xC30C,
};

static const int32_t k_tap6[6] = {0x2B57B, -0xF88AE, 0x4CD3AE, 0x4CD3AE, -0xF88AE, 0x2B57B};

static const int32_t k_lfo[32] = {
    0x19999A,  0x30A3D7,  0x428F5C,  0x547AE1,  0x6147AE,  0x68F5C3,  0x70A3D7,  0x770A3D,
    0x7AE148,  0x7D70A4,  0x7851EC,  0x6F5C29,  0x600000,  0x4B851F,  0x347AE1,  0x1AE148,
    -0x19999A, -0x30A3D7, -0x428F5C, -0x547AE1, -0x6147AE, -0x68F5C3, -0x70A3D7, -0x770A3D,
    -0x7AE148, -0x7D70A4, -0x7851EC, -0x6F5C29, -0x600000, -0x4B851F, -0x347AE1, -0x1AE148,
};

/* P:$010A: the 16-tap filter over the history into the filter ring, each
 * output with the raw centre word before it. The coefficient pointer turns
 * once an output, so tap k always meets the word k samples back, and the
 * table is symmetric, so taps k and 15-k share a coefficient. */
static void filter16(ifx_chorus_core_state *state, const ifx_chorus_core_layout *layout,
                     int32_t memory[0x10000]) {
  uint32_t r1 = state->history_write;
  uint32_t r5 = state->filter_write;
  /* The ring is a power-of-two window, so its mask is the modulo. */
  const uint32_t m = layout->filter_m;
  const uint32_t stride = (uint32_t)layout->filter_stride;
  for (unsigned out = 0; out < IFX_BLOCK_FRAMES; out++) {
    int64_t a = 0;
    for (unsigned tap = 0; tap < 7u; tap++) {
      a += ifx_product(k_tap16[tap], state->history[(r1 - tap) & HISTORY_M] +
                                         state->history[(r1 - (15u - tap)) & HISTORY_M]);
    }
    a = ifx_macr(a, k_tap16[7],
                 state->history[(r1 - 7u) & HISTORY_M] + state->history[(r1 - 8u) & HISTORY_M]);
    memory[r5] = state->history[(r1 - 8u) & HISTORY_M];
    r5 = (r5 & ~m) | ((r5 + stride) & m);
    memory[r5] = ifx_store24(a);
    r5 = (r5 & ~m) | ((r5 + stride) & m);
    r1 = ifx_agu_step(r1, 1, HISTORY_M);
  }
  state->filter_write = r5;
  state->history_write = r1;
}

/* P:$011E: configuration 4's 6-tap filter over every other word of the ring.
 * The table is symmetric, so taps k and 5-k share a coefficient. */
static void filter6(ifx_chorus_core_state *state, int32_t memory[0x10000]) {
  uint32_t r3 = state->filter_read;
  uint32_t r5 = state->filter2_write;
  for (unsigned out = 0; out < 2u * IFX_BLOCK_FRAMES; out++) {
    uint32_t r4 = r3;
    uint32_t r6 = ifx_agu_step(r3, 10, FILTER6_M);
    int64_t a = 0;
    for (unsigned tap = 0; tap < 2u; tap++) {
      a += ifx_product(k_tap6[tap], memory[r4] + memory[r6]);
      r4 = ifx_agu_step(r4, 2, FILTER6_M);
      r6 = ifx_agu_step(r6, -2, FILTER6_M);
    }
    a = ifx_macr(a, k_tap6[2], memory[r4] + memory[r6]);
    r3 = ifx_agu_step(r3, 2, FILTER6_M);
    memory[r5] = ifx_store24(a);
    r5 = ifx_agu_step(r5, 2, FILTER6_M);
  }
  state->filter_read = r3;
  state->filter2_write = r5;
}

/* P:$0132..$018B: resynchronise a voice's LFO near the filter pointer, advance
 * it, and read the 32 interpolated taps its sweep points at. */
static void sweep_voice(const ifx_chorus_core_state *state, const ifx_chorus_core_layout *layout,
                        const int32_t voice[8], int32_t phase[4], const int32_t memory[0x10000],
                        int32_t out[IFX_BLOCK_FRAMES]) {
  const uint32_t filter = layout->clamp_filter2 != 0 ? state->filter2_write : state->filter_write;
  int64_t a = ifx_word_acc((int32_t)filter - SWEEP_WINDOW);
  a = ifx_sub(a, ifx_word_acc(phase[0]));
  if (ifx_abs56(a) < ifx_abs56(ifx_word_acc(layout->clamp_window))) {
    phase[2] = a > 0 ? 0x10 : 0;
    phase[3] = 0;
  }

  a = ifx_set_a0(ifx_set_a1(0, phase[2]), phase[3]);
  a = ifx_acc56(a + (int64_t)(uint32_t)voice[0]);
  a = ifx_and_a1(a, LFO_M);
  phase[2] = ifx_move_a1(a);
  phase[3] = ifx_move_a0(a);
  uint32_t r4 = (uint32_t)ifx_a1(a);
  a = ifx_asr(ifx_lsl_a1(a));
  const int32_t fraction = ifx_move_a0(a);
  r4 = ifx_agu_step(r4, layout->lfo_base, LFO_M);
  a = ifx_word_acc(k_lfo[r4 & LFO_M]);
  r4 = ifx_agu_step(r4, 1, LFO_M);
  a = ifx_mac(a, -fraction, ifx_move_a1(a));
  a = ifx_macr(a, k_lfo[r4 & LFO_M], fraction);
  int64_t b = ifx_mpyr(ifx_move_a1(a), voice[1]);
  if (layout->extra_asr != 0) {
    b = ifx_asr(b);
  }
  a = ifx_add(ifx_set_a2(0, layout->extra_asr != 0 ? 2 : 4), b);
  const uint32_t step_low = (uint32_t)ifx_a1(a);
  const uint32_t step_high = (uint32_t)ifx_a2(a);
  uint32_t index = (uint32_t)phase[0] & 0xFFFFFFu;
  uint32_t low = (uint32_t)phase[1] & 0xFFFFFFu;
  /* The ring is a power-of-two window, so its mask is the modulo. */
  const uint32_t m = layout->sweep_m;
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    low += step_low;
    index = (index + step_high + (low >> 24)) & m;
    low &= 0xFFFFFFu;
    const int32_t tap_fraction = (int32_t)(low >> 1);
    /* $2000 is a whole number of sweep windows, so the base add steps linearly. */
    const int32_t y0 = memory[index + SWEEP_WINDOW];
    const int32_t y1 = memory[((index + 1u) & m) + SWEEP_WINDOW];
    out[i] = ifx_store24(ifx_round24(ifx_word_acc(y0) + ifx_product(y1 - y0, tap_fraction)));
  }
  phase[0] = (int32_t)index;
  phase[1] = ifx_s24(low);
}

void ifx_chorus_core_state_init(ifx_chorus_core_state *state, const ifx_chorus_core_layout *layout,
                                int32_t regen_l, int32_t regen_r) {
  ifx_regen_core_state_init(&state->regen, regen_l, regen_r);
  for (unsigned i = 0; i < IFX_REGEN_CORE_HISTORY_WORDS; i++) {
    state->history[i] = 0;
  }
  for (unsigned i = 0; i < 2u * IFX_BLOCK_FRAMES; i++) {
    state->voice_bus[i] = 0;
  }
  for (unsigned voice = 0; voice < IFX_CHORUS_CORE_MAX_VOICES; voice++) {
    state->phase[voice][0] = voice < layout->voices ? layout->phase_step : 0;
    state->phase[voice][1] = 0;
    state->phase[voice][2] = 0;
    state->phase[voice][3] = 0;
  }
  state->history_write = HISTORY_BASE;
  state->filter_write = layout->filter_write;
  state->filter_read = layout->filter_read;
  state->filter2_write = layout->filter2_write;
  state->read_l = LINE_L;
  state->read_r = LINE_R;
}

void ifx_chorus_core_process(ifx_chorus_core_state *state, const ifx_chorus_core_layout *layout,
                             int32_t memory[0x10000], const int32_t *voices, int32_t regen_l,
                             int32_t regen_r, const int32_t input[IFX_BLOCK_FRAMES]) {
  ifx_regen_core_stage(&state->regen, regen_l, regen_r);
  ifx_regen_core_limit_voices(&state->regen, voices, layout->voices);
  ifx_regen_core_feed(&state->regen, memory, state->read_l, state->read_r, input, state->history,
                      state->history_write);
  filter16(state, layout, memory);
  if (layout->six_tap != 0) {
    filter6(state, memory);
  }
  for (unsigned v = 0; v < layout->voices; v++) {
    const int32_t *const voice = &voices[8u * v];
    int32_t out[IFX_BLOCK_FRAMES];
    sweep_voice(state, layout, voice, state->phase[v], memory, out);
    ifx_regen_core_add(memory, layout->voice_bus != 0 ? state->voice_bus : NULL, state->read_l,
                       state->read_r, voice[3], voice[6], voice[7], out);
  }
}
