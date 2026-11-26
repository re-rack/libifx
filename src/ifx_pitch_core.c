#include "ifx_pitch_core.h"

#include "ifx_dsp_math.h"

enum {
  HISTORY_BASE = 0x40,
  HISTORY_M = 0x3F,
  TAP16_BASE = 0x30,
  TAP16_M = 0x0F,
  TAP6_BASE = 0x38,
  TAP6_M = 0x05,
  LIMIT_COEF = 0x71EB85,
  WINDOW_STEP = 0x008081,
  WINDOW_TOP = 0x800000,
};

static const int32_t k_tap16[16] = {
    0xFF9E7A, 0x00D100, 0xFE80CE, 0x02883A, 0xFBDC5F, 0x06DF72, 0xF33748, 0x2878F6,
    0x2878F6, 0xF33748, 0x06DF72, 0xFBDC5F, 0x02883A, 0xFE80CE, 0x00D100, 0xFF9E7A,
};

static const int32_t k_tap6[6] = {0x02B57B, 0xF07752, 0x4CD3AE, 0x4CD3AE, 0xF07752, 0x02B57B};

static int32_t word_of(int64_t acc) { return ifx_s24((uint32_t)ifx_a1(acc)); }

static void build_window(int32_t memory[0x10000], unsigned base) {
  int64_t a = 0;
  for (unsigned i = 0; i < 128u; i++) {
    const int32_t y0 = ifx_a1(a);
    a = ifx_add24(a, WINDOW_STEP);
    int64_t b = ifx_asl(ifx_mpyr(y0, y0));
    memory[(base + i) & 0xFFFFu] = ifx_store24(b);
  }
  const int64_t top = ifx_word_acc(WINDOW_TOP);
  for (unsigned i = 0; i < 128u; i++) {
    int64_t b = ifx_sub24(top, memory[(base + 127u - i) & 0xFFFFu]);
    memory[(base + 128u + i) & 0xFFFFu] = ifx_store24(b);
  }
}

void ifx_pitch_core_state_init(ifx_pitch_core_state *state, const ifx_pitch_core_layout *layout,
                               int32_t memory[0x10000], int32_t regen_l, int32_t regen_r) {
  for (unsigned i = 0; i < 64u; i++) {
    state->history[i] = 0;
  }
  for (unsigned i = 0; i < 2u; i++) {
    for (unsigned j = 0; j < 32u; j++) {
      state->grain[i][j] = 0;
    }
  }
  for (unsigned i = 0; i < 64u; i++) {
    state->voice_bus[i] = 0;
  }
  for (unsigned voice = 0; voice < IFX_PITCH_MAX_VOICES; voice++) {
    state->voice[voice][0] = (int32_t)layout->grain_phase;
    state->voice[voice][1] = 0;
    state->voice[voice][2] = 0;
    state->voice[voice][3] = 0;
    state->voice[voice][4] = 0;
    state->voice[voice][5] = 0;
    state->voice[voice][6] = 0;
  }
  build_window(memory, layout->window_base);
  state->history_write = HISTORY_BASE;
  state->filter1_write = layout->filter1_base;
  state->filter2_write = layout->filter2_base;
  state->read_l = layout->line_l;
  state->read_r = layout->line_r;
  state->regen_l = regen_l;
  state->regen_r = regen_r;
  state->staged_regen_l = regen_l;
  state->staged_regen_r = regen_r;
}

/* P:$00B6..$00CF: the Hush block plus the regenerated delay output into
 * X:$40..$7F. */
static void sum_input(ifx_pitch_core_state *state, const ifx_pitch_core_layout *layout,
                      const int32_t *memory, const int32_t input[32]) {
  int32_t *const history = state->history;
  uint32_t r1 = state->history_write;
  const uint32_t m1 = HISTORY_M;
  uint32_t r5 = state->read_l;
  uint32_t r6 = state->read_r;
  const uint32_t m5 = layout->line_m;
  const uint32_t m6 = layout->line_m;
  const int32_t x0 = state->regen_l;
  const int32_t x1 = state->regen_r;
  unsigned sample = 0;
  int32_t y0 = memory[r5];
  r5 = ifx_agu_step(r5, 1, m5);
  int64_t b = ifx_word_acc(input[sample++]);
  int32_t y1 = memory[r6];
  r6 = ifx_agu_step(r6, 1, m6);
  b = ifx_mac(b, y0, x0);
  y0 = memory[r5];
  r5 = ifx_agu_step(r5, 1, m5);
  b = ifx_macr(b, y1, x1);
  int64_t a = ifx_word_acc(input[sample++]);
  y1 = memory[r6];
  r6 = ifx_agu_step(r6, 1, m6);
  for (unsigned i = 0; i < 15u; i++) {
    a = ifx_mac(a, y0, x0);
    history[r1 & 0x3Fu] = ifx_store24(b);
    r1 = ifx_agu_step(r1, 1, m1);
    y0 = memory[r5];
    r5 = ifx_agu_step(r5, 1, m5);
    a = ifx_macr(a, y1, x1);
    b = ifx_word_acc(input[sample++]);
    y1 = memory[r6];
    r6 = ifx_agu_step(r6, 1, m6);
    b = ifx_mac(b, y0, x0);
    history[r1 & 0x3Fu] = ifx_store24(a);
    r1 = ifx_agu_step(r1, 1, m1);
    y0 = memory[r5];
    r5 = ifx_agu_step(r5, 1, m5);
    b = ifx_macr(b, y1, x1);
    a = ifx_word_acc(input[sample++]);
    y1 = memory[r6];
    r6 = ifx_agu_step(r6, 1, m6);
  }
  a = ifx_mac(a, y0, x0);
  history[r1 & 0x3Fu] = ifx_store24(b);
  r1 = ifx_agu_step(r1, 1, m1);
  a = ifx_macr(a, y1, x1);
  history[r1 & 0x3Fu] = ifx_store24(a);
}

/* P:$00D0..$00E6: the 16-tap filter into the 128-word ring (configuration 5)
 * or straight into the large ring (configuration 1). */
static void filter16(ifx_pitch_core_state *state, const ifx_pitch_core_layout *layout,
                     int32_t memory[0x10000], unsigned into_filter1) {
  const uint32_t m1 = HISTORY_M;
  uint32_t r1 = ifx_agu_step(state->history_write, -15, m1);
  uint32_t r2 = ifx_agu_step(r1, -15, m1);
  uint32_t r4 = TAP16_BASE;
  const uint32_t m4 = TAP16_M;
  uint32_t r6 = into_filter1 != 0u ? state->filter1_write : state->filter2_write;
  const uint32_t m6 = into_filter1 != 0u ? layout->filter1_m : layout->filter2_m;
  int64_t a = ifx_word_acc(state->history[r1 & 0x3Fu]);
  r1 = ifx_agu_step(r1, 1, m1);
  for (unsigned out = 0; out < 32u; out++) {
    a = ifx_asr(a);
    int32_t x0 = state->history[r2 & 0x3Fu];
    r2 = ifx_agu_step(r2, 2, m1);
    int32_t y0 = ifx_s24((uint32_t)k_tap16[r4 - TAP16_BASE]);
    r4 = ifx_agu_step(r4, 1, m4);
    for (unsigned tap = 0; tap < 15u; tap++) {
      a = ifx_mac(a, y0, x0);
      x0 = state->history[r2 & 0x3Fu];
      r2 = ifx_agu_step(r2, 2, m1);
      y0 = ifx_s24((uint32_t)k_tap16[r4 - TAP16_BASE]);
      r4 = ifx_agu_step(r4, 1, m4);
    }
    a = ifx_macr(a, y0, x0);
    r2 = ifx_agu_step(r1, -15, m1);
    memory[r6 & 0xFFFFu] = ifx_store24(a);
    r6 = ifx_agu_step(r6, 1, m6);
    a = ifx_word_acc(state->history[r1 & 0x3Fu]);
    r1 = ifx_agu_step(r1, 1, m1);
  }
  if (layout->delay_voice == 0u) {
    r1 = ifx_agu_step(r1, -1, m1);
    r1 = ifx_agu_step(r1, 15, m1);
    state->history_write = r1;
  }
  if (into_filter1 == 0u) {
    state->filter2_write = r6;
  }
}

/* P:$00E7..$00FD: the 6-tap filter, writing the raw centre word and the
 * filtered word into the large ring. */
static void filter6(ifx_pitch_core_state *state, const ifx_pitch_core_layout *layout,
                    int32_t memory[0x10000]) {
  uint32_t r1 = TAP6_BASE;
  const uint32_t m1 = TAP6_M;
  uint32_t r5 = state->filter1_write;
  const uint32_t m5 = layout->filter1_m;
  uint32_t r4 = r5;
  const uint32_t m4 = layout->filter1_m;
  uint32_t r6 = state->filter2_write;
  const uint32_t m6 = layout->filter2_m;
  for (unsigned out = 0; out < 32u; out++) {
    int64_t a = 0;
    int32_t x0 = ifx_s24((uint32_t)k_tap6[r1 - TAP6_BASE]);
    r1 = ifx_agu_step(r1, 1, m1);
    int32_t y0 = memory[r4 & 0xFFFFu];
    r4 = ifx_agu_step(r4, -1, m4);
    for (unsigned tap = 0; tap < 5u; tap++) {
      a = ifx_mac(a, y0, x0);
      x0 = ifx_s24((uint32_t)k_tap6[r1 - TAP6_BASE]);
      r1 = ifx_agu_step(r1, 1, m1);
      y0 = memory[r4 & 0xFFFFu];
      r4 = ifx_agu_step(r4, -1, m4);
      if (tap == 2u) {
        memory[r6 & 0xFFFFu] = y0;
        r6 = ifx_agu_step(r6, 1, m6);
      }
    }
    a = ifx_macr(a, y0, x0);
    r5 = ifx_agu_step(r5, 1, m5);
    r4 = r5;
    memory[r6 & 0xFFFFu] = ifx_store24(a);
    r6 = ifx_agu_step(r6, 1, m6);
  }
  state->filter1_write = r5;
  state->filter2_write = r6;
}

/* P:$0169..$0191: one grain's 32 swept interpolated reads into its ring. */
static void grain_read(const ifx_pitch_core_layout *layout, const int32_t *memory, int32_t *voice,
                       int32_t rate, int32_t *grain) {
  const uint32_t phase_mask = (1u << layout->phase_bits) - 1u;
  int64_t a = ifx_acc56(((int64_t)voice[0] << 24) | (int64_t)((uint32_t)voice[1] & 0xFFFFFFu));
  int64_t b = (int64_t)((uint32_t)rate & 0xFFFFFFu);
  b = ifx_asl_n(b, layout->rate_shift);
  a = ifx_add(a, b);
  a = ifx_and_a1(a, (int32_t)phase_mask);

  uint32_t integer[32];
  int32_t fraction[32];
  for (unsigned i = 0; i < 31u; i++) {
    integer[i] = (uint32_t)ifx_a1(a) & phase_mask;
    fraction[i] = ifx_a0(a);
    a = ifx_add(a, b);
    a = ifx_and_a1(a, (int32_t)phase_mask);
  }
  integer[31] = (uint32_t)ifx_a1(a) & phase_mask;
  fraction[31] = ifx_a0(a);
  voice[0] = ifx_s24((uint32_t)ifx_a1(a));
  voice[1] = ifx_a0(a);

  for (unsigned i = 0; i < 32u; i++) {
    const int32_t x1 = (int32_t)((uint32_t)fraction[i] >> 1);
    uint32_t address = (layout->filter2_base + integer[i]) & 0xFFFFu;
    const int32_t s0 = memory[address];
    address = ifx_agu_step(address, 1, layout->filter2_m);
    const int32_t s1 = memory[address];
    int64_t out = ifx_word_acc(s0);
    out = ifx_mac(out, -x1, s0);
    out = ifx_macr(out, x1, s1);
    grain[i] = ifx_store24(out);
  }
}

/* P:$0106..$0154: the voices, each two grains crossfaded by the window and
 * added into both delay lines at its delay. */
static void voices_block(ifx_pitch_core_state *state, const ifx_pitch_core_layout *layout,
                         int32_t memory[0x10000], const int32_t *voices) {
  for (unsigned v = 0; v < layout->voices; v++) {
    int32_t *const voice = state->voice[v];
    grain_read(layout, memory, voice, voices[v * 8u + 2u], state->grain[0]);
    grain_read(layout, memory, voice + 3, voices[v * 8u + 2u], state->grain[1]);

    int64_t delta = ifx_word_acc((int32_t)state->filter2_write - (int32_t)layout->filter2_base);
    delta = ifx_sub24(delta, ifx_s24((uint32_t)voice[3]));
    delta = ifx_abs56(delta);
    int32_t distance = ifx_s24((uint32_t)ifx_a1(delta));
    const int32_t half = (int32_t)((layout->filter2_m + 1u) >> 1);
    if (distance > half) {
      distance = (int32_t)(layout->filter2_m + 1u) - distance;
    }
    const int64_t scale = (int64_t)255 << (24u - layout->phase_bits);
    int64_t scaled = ifx_mpy(distance, (int32_t)scale);
    const unsigned index = (unsigned)ifx_a1(scaled) & 0xFFu;
    int64_t window = ifx_word_acc(memory[(layout->window_base + index) & 0xFFFFu]);
    int64_t run = ifx_sub24(window, voice[6]);
    run = ifx_asr_n(run, 5u);
    const int32_t step = word_of(run);
    run = ifx_add24(run, voice[6]);
    voice[6] = word_of(window);
    int32_t x0 = word_of(run);
    int32_t y0 = state->grain[0][0];
    for (unsigned i = 0; i < 32u; i++) {
      run = ifx_add24(run, step);
      int64_t a = ifx_word_acc(y0);
      a = ifx_mac(a, -x0, y0);
      a = ifx_macr(a, x0, state->grain[1][i]);
      y0 = state->grain[0][(i + 1u) & 31u];
      state->grain[0][i] = ifx_store24(a);
      x0 = word_of(run);
    }

    const int32_t delay = voices[v * 8u + 3u];
    const int bus = layout->voice_bus != 0u && delay == 0;
    uint32_t r3 = bus ? 0u : ifx_agu_step(state->read_l, delay, layout->line_m);
    uint32_t r6 = bus ? 32u : ifx_agu_step(state->read_r, delay, layout->line_m);
    const int32_t gain_l = voices[v * 8u + 6u];
    const int32_t gain_r = voices[v * 8u + 7u];
    for (unsigned i = 0; i < 32u; i++) {
      const int32_t sample = state->grain[0][i];
      int64_t a = ifx_word_acc(bus ? state->voice_bus[r3] : memory[r3 & 0xFFFFu]);
      a = ifx_macr(a, gain_l, sample);
      if (bus) {
        state->voice_bus[r3++] = ifx_store24(a);
      } else {
        memory[r3 & 0xFFFFu] = ifx_store24(a);
        r3 = ifx_agu_step(r3, 1, layout->line_m);
      }
      int64_t b = ifx_word_acc(bus ? state->voice_bus[r6] : memory[r6 & 0xFFFFu]);
      b = ifx_macr(b, gain_r, sample);
      if (bus) {
        state->voice_bus[r6++] = ifx_store24(b);
      } else {
        memory[r6 & 0xFFFFu] = ifx_store24(b);
        r6 = ifx_agu_step(r6, 1, layout->line_m);
      }
    }
  }

  if (layout->delay_voice != 0u) {
    const int32_t *const voice = &voices[layout->voices * 8u];
    uint32_t r3 = state->history_write;
    uint32_t r5 = ifx_agu_step(state->read_l, voice[3], layout->line_m);
    uint32_t r6 = ifx_agu_step(state->read_r, voice[3], layout->line_m);
    const int32_t gain_l = voice[6];
    const int32_t gain_r = voice[7];
    for (unsigned i = 0; i < 32u; i++) {
      const int32_t sample = state->history[r3 & 0x3Fu];
      r3 = ifx_agu_step(r3, 1, HISTORY_M);
      int64_t a = ifx_word_acc(memory[r5 & 0xFFFFu]);
      a = ifx_macr(a, gain_l, sample);
      memory[r5 & 0xFFFFu] = ifx_store24(a);
      r5 = ifx_agu_step(r5, 1, layout->line_m);
      int64_t b = ifx_word_acc(memory[r6 & 0xFFFFu]);
      b = ifx_macr(b, gain_r, sample);
      memory[r6 & 0xFFFFu] = ifx_store24(b);
      r6 = ifx_agu_step(r6, 1, layout->line_m);
    }
    state->history_write = r3;
  }
}

void ifx_pitch_core_process(ifx_pitch_core_state *state, const ifx_pitch_core_layout *layout,
                            int32_t memory[0x10000], const int32_t *voices, int32_t regen_l,
                            int32_t regen_r, const int32_t input[32], int32_t out_l[32],
                            int32_t out_r[32]) {
  if (state->staged_regen_l != regen_l) {
    state->staged_regen_l = regen_l;
    state->regen_l = regen_l;
  }
  if (state->staged_regen_r != regen_r) {
    state->staged_regen_r = regen_r;
    state->regen_r = regen_r;
  }

  /* P:$00A2: the regeneration limiter over the voices' pan gains. */
  {
    int64_t a = 0;
    const int32_t x0 = state->regen_l;
    const int32_t x1 = state->regen_r;
    const unsigned count = layout->voices + layout->delay_voice;
    for (unsigned v = 0; v < count; v++) {
      a = ifx_mac(a, voices[v * 8u + 6u], x0);
      a = ifx_mac(a, x1, voices[v * 8u + 7u]);
    }
    if (ifx_es(a)) {
      state->regen_l = ifx_store24(ifx_mpyr(LIMIT_COEF, x0));
      state->regen_r = ifx_store24(ifx_mpyr(x1, LIMIT_COEF));
    }
  }

  sum_input(state, layout, memory, input);
  if (layout->dual_filter != 0u) {
    filter16(state, layout, memory, 1u);
    filter6(state, layout, memory);
  } else {
    filter16(state, layout, memory, 0u);
  }
  voices_block(state, layout, memory, voices);

  /* P:$0155..$0168: the oldest block of both lines into the output. */
  if (layout->final_line_read != 0u) {
    uint32_t r3 = state->read_l;
    uint32_t r4 = state->read_r;
    for (unsigned i = 0; i < 32u; i++) {
      out_l[i] = memory[r3 & 0xFFFFu];
      memory[r3 & 0xFFFFu] = 0;
      r3 = ifx_agu_step(r3, 1, layout->line_m);
      out_r[i] = memory[r4 & 0xFFFFu];
      memory[r4 & 0xFFFFu] = 0;
      r4 = ifx_agu_step(r4, 1, layout->line_m);
    }
    state->read_l = r3;
    state->read_r = r4;
  }
}
