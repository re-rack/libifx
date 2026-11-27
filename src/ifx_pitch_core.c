#include "ifx_pitch_core.h"

#include <stddef.h>

#include "ifx_dsp_math.h"

enum {
  HISTORY_BASE = 0x40,
  HISTORY_M = IFX_REGEN_CORE_HISTORY_WORDS - 1u,
  TAP16_BASE = 0x30,
  TAP16_M = 0x0F,
  TAP6_BASE = 0x38,
  TAP6_M = 0x05,
  WINDOW_HALF = 128,
  WINDOW_STEP = 0x008081,
  WINDOW_TOP = 0x800000,
};

static const uint32_t k_tap16[16] = {
    0xFF9E7A, 0x00D100, 0xFE80CE, 0x02883A, 0xFBDC5F, 0x06DF72, 0xF33748, 0x2878F6,
    0x2878F6, 0xF33748, 0x06DF72, 0xFBDC5F, 0x02883A, 0xFE80CE, 0x00D100, 0xFF9E7A,
};

static const uint32_t k_tap6[6] = {0x02B57B, 0xF07752, 0x4CD3AE, 0x4CD3AE, 0xF07752, 0x02B57B};

/* The 256-word crossfade window: a squared rise to 0.5, then mirrored up to 1.0. */
static void build_window(int32_t memory[0x10000], uint32_t base) {
  int64_t a = 0;
  for (unsigned i = 0; i < WINDOW_HALF; i++) {
    const int32_t y0 = ifx_move_a1(a);
    a = ifx_add24(a, WINDOW_STEP);
    memory[(base + i) & 0xFFFFu] = ifx_store24(ifx_asl(ifx_mpyr(y0, y0)));
  }
  for (unsigned i = 0; i < WINDOW_HALF; i++) {
    const int64_t b = ifx_sub24(ifx_word_acc(WINDOW_TOP), memory[(base + 127u - i) & 0xFFFFu]);
    memory[(base + WINDOW_HALF + i) & 0xFFFFu] = ifx_store24(b);
  }
}

/* P:$00D0..$00E6: the 16-tap filter into the 128-word ring (configuration 5)
 * or straight into the grain ring (configuration 1). */
static void filter16(ifx_pitch_core_state *state, const ifx_pitch_core_layout *layout,
                     int32_t memory[0x10000]) {
  uint32_t r1 = ifx_agu_step(state->history_write, -15, HISTORY_M);
  uint32_t r2 = ifx_agu_step(r1, -15, HISTORY_M);
  uint32_t r4 = TAP16_BASE;
  uint32_t r6 = layout->six_tap != 0 ? state->filter1_write : state->filter2_write;
  const uint32_t m6 = layout->six_tap != 0 ? layout->filter1_m : layout->filter2_m;
  int64_t a = ifx_word_acc(state->history[r1 & HISTORY_M]);
  r1 = ifx_agu_step(r1, 1, HISTORY_M);
  for (unsigned out = 0; out < IFX_BLOCK_FRAMES; out++) {
    a = ifx_asr(a);
    int32_t x0 = state->history[r2 & HISTORY_M];
    r2 = ifx_agu_step(r2, 2, HISTORY_M);
    int32_t y0 = ifx_s24(k_tap16[r4 - TAP16_BASE]);
    r4 = ifx_agu_step(r4, 1, TAP16_M);
    for (unsigned tap = 0; tap < 15u; tap++) {
      a = ifx_mac(a, y0, x0);
      x0 = state->history[r2 & HISTORY_M];
      r2 = ifx_agu_step(r2, 2, HISTORY_M);
      y0 = ifx_s24(k_tap16[r4 - TAP16_BASE]);
      r4 = ifx_agu_step(r4, 1, TAP16_M);
    }
    a = ifx_macr(a, y0, x0);
    r2 = ifx_agu_step(r1, -15, HISTORY_M);
    memory[r6] = ifx_store24(a);
    r6 = ifx_agu_step(r6, 1, m6);
    a = ifx_word_acc(state->history[r1 & HISTORY_M]);
    r1 = ifx_agu_step(r1, 1, HISTORY_M);
  }
  if (layout->delay_voice == 0) {
    state->history_write = ifx_agu_step(ifx_agu_step(r1, -1, HISTORY_M), 15, HISTORY_M);
  }
  if (layout->six_tap == 0) {
    state->filter2_write = r6;
  }
}

/* P:$00E7..$00FD: the 6-tap filter, writing the raw centre word and the
 * filtered word into the grain ring. */
static void filter6(ifx_pitch_core_state *state, const ifx_pitch_core_layout *layout,
                    int32_t memory[0x10000]) {
  uint32_t r1 = TAP6_BASE;
  uint32_t r5 = state->filter1_write;
  uint32_t r4 = r5;
  uint32_t r6 = state->filter2_write;
  for (unsigned out = 0; out < IFX_BLOCK_FRAMES; out++) {
    int64_t a = 0;
    int32_t x0 = ifx_s24(k_tap6[r1 - TAP6_BASE]);
    r1 = ifx_agu_step(r1, 1, TAP6_M);
    int32_t y0 = memory[r4];
    r4 = ifx_agu_step(r4, -1, layout->filter1_m);
    for (unsigned tap = 0; tap < 5u; tap++) {
      a = ifx_mac(a, y0, x0);
      x0 = ifx_s24(k_tap6[r1 - TAP6_BASE]);
      r1 = ifx_agu_step(r1, 1, TAP6_M);
      y0 = memory[r4];
      r4 = ifx_agu_step(r4, -1, layout->filter1_m);
      if (tap == 2u) {
        memory[r6] = y0;
        r6 = ifx_agu_step(r6, 1, layout->filter2_m);
      }
    }
    a = ifx_macr(a, y0, x0);
    r5 = ifx_agu_step(r5, 1, layout->filter1_m);
    r4 = r5;
    memory[r6] = ifx_store24(a);
    r6 = ifx_agu_step(r6, 1, layout->filter2_m);
  }
  state->filter1_write = r5;
  state->filter2_write = r6;
}

/* P:$0169..$0191: one grain's 32 swept interpolated reads of the grain ring;
 * `phase` holds its integer and fraction words. */
static void grain_read(const ifx_pitch_core_layout *layout, const int32_t memory[0x10000],
                       int32_t phase[2], int32_t rate, int32_t grain[IFX_BLOCK_FRAMES]) {
  const int32_t phase_mask = (1 << layout->phase_bits) - 1;
  const int64_t b = ifx_asl_n(ifx_set_a0(0, rate), layout->rate_shift);
  int64_t a = ifx_set_a0(ifx_word_acc(phase[0]), phase[1]);
  uint32_t integer[IFX_BLOCK_FRAMES];
  int32_t fraction[IFX_BLOCK_FRAMES];
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    a = ifx_and_a1(ifx_add(a, b), phase_mask);
    integer[i] = (uint32_t)ifx_a1(a);
    fraction[i] = (int32_t)((uint32_t)ifx_a0(a) >> 1);
  }
  phase[0] = ifx_move_a1(a);
  phase[1] = ifx_move_a0(a);

  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    uint32_t address = (layout->filter2_base + integer[i]) & 0xFFFFu;
    const int32_t s0 = memory[address];
    address = ifx_agu_step(address, 1, layout->filter2_m);
    int64_t out = ifx_word_acc(s0);
    out = ifx_mac(out, -fraction[i], s0);
    out = ifx_macr(out, fraction[i], memory[address]);
    grain[i] = ifx_store24(out);
  }
}

/* P:$0106..$0154: one voice's two grains crossfaded by the window, which
 * follows the distance between the second grain and the filter pointer. */
static void voice_block(const ifx_pitch_core_layout *layout, const int32_t memory[0x10000],
                        uint32_t filter2_write, int32_t voice[7], int32_t rate,
                        int32_t out[IFX_BLOCK_FRAMES]) {
  int32_t second[IFX_BLOCK_FRAMES];
  grain_read(layout, memory, &voice[0], rate, out);
  grain_read(layout, memory, &voice[3], rate, second);

  int64_t delta = ifx_word_acc((int32_t)filter2_write - (int32_t)layout->filter2_base);
  delta = ifx_abs56(ifx_sub24(delta, voice[3]));
  int32_t distance = ifx_move_a1(delta);
  const int32_t ring = (int32_t)layout->filter2_m + 1;
  if (distance > ring / 2) {
    distance = ring - distance;
  }
  const int32_t scale = 255 << (24u - layout->phase_bits);
  const unsigned index = (unsigned)ifx_a1(ifx_mpy(distance, scale)) & 0xFFu;
  const int64_t window = ifx_word_acc(memory[(layout->window_base + index) & 0xFFFFu]);
  int64_t run = ifx_asr_n(ifx_sub24(window, voice[6]), 5u);
  const int32_t step = ifx_move_a1(run);
  run = ifx_add24(run, voice[6]);
  voice[6] = ifx_move_a1(window);
  int32_t x0 = ifx_move_a1(run);
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    run = ifx_add24(run, step);
    int64_t a = ifx_word_acc(out[i]);
    a = ifx_mac(a, -x0, out[i]);
    a = ifx_macr(a, x0, second[i]);
    out[i] = ifx_store24(a);
    x0 = ifx_move_a1(run);
  }
}

void ifx_pitch_core_state_init(ifx_pitch_core_state *state, const ifx_pitch_core_layout *layout,
                               int32_t memory[0x10000], int32_t regen_l, int32_t regen_r) {
  ifx_regen_core_state_init(&state->regen, regen_l, regen_r);
  for (unsigned i = 0; i < IFX_REGEN_CORE_HISTORY_WORDS; i++) {
    state->history[i] = 0;
  }
  for (unsigned voice = 0; voice < IFX_PITCH_CORE_MAX_VOICES; voice++) {
    for (unsigned word = 0; word < 7u; word++) {
      state->voice[voice][word] = 0;
    }
    state->voice[voice][0] = layout->grain_phase;
  }
  for (unsigned i = 0; i < 2u * IFX_BLOCK_FRAMES; i++) {
    state->voice_bus[i] = 0;
  }
  build_window(memory, layout->window_base);
  state->history_write = HISTORY_BASE;
  state->filter1_write = layout->filter1_base;
  state->filter2_write = layout->filter2_base;
  state->read_l = layout->line_l;
  state->read_r = layout->line_r;
}

void ifx_pitch_core_process(ifx_pitch_core_state *state, const ifx_pitch_core_layout *layout,
                            int32_t memory[0x10000], const int32_t *voices, int32_t regen_l,
                            int32_t regen_r, const int32_t input[IFX_BLOCK_FRAMES]) {
  ifx_regen_core_stage(&state->regen, regen_l, regen_r);
  ifx_regen_core_limit_voices(&state->regen, voices,
                              layout->voices + (layout->delay_voice != 0 ? 1u : 0u));
  ifx_regen_core_feed(&state->regen, memory, state->read_l, state->read_r, input, state->history,
                      state->history_write);
  filter16(state, layout, memory);
  if (layout->six_tap != 0) {
    filter6(state, layout, memory);
  }

  int32_t *const bus = layout->voice_bus != 0 ? state->voice_bus : NULL;
  for (unsigned v = 0; v < layout->voices; v++) {
    const int32_t *const words = &voices[8u * v];
    int32_t out[IFX_BLOCK_FRAMES];
    voice_block(layout, memory, state->filter2_write, state->voice[v], words[2], out);
    ifx_regen_core_add(memory, bus, state->read_l, state->read_r, words[3], words[6], words[7],
                       out);
  }

  /* Configuration 1's Delay 3 reads the history the block just wrote. */
  if (layout->delay_voice != 0) {
    const int32_t *const words = &voices[8u * layout->voices];
    int32_t out[IFX_BLOCK_FRAMES];
    for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
      out[i] = state->history[state->history_write & HISTORY_M];
      state->history_write = ifx_agu_step(state->history_write, 1, HISTORY_M);
    }
    ifx_regen_core_add(memory, NULL, state->read_l, state->read_r, words[3], words[6], words[7],
                       out);
  }
}
