#include "ifx_chorus_core.h"

#include "ifx_dsp_math.h"

enum {
  HISTORY_BASE = 0x40,
  SWEEP_BASE = 0x40,
  TAP_BASE = 0x30,
  TAP6_BASE = 0x38,
  FILTER_WRITE = 0x200C,
  FILTER_READ = 0x2000,
  FILTER2_WRITE = 0x2005,
  LINE_L = 0x4000,
  LINE_R = 0x8000,
  PHASE_STEP = 0x1000,
  LIMIT_COEF = 0x71EB85,
};

static const uint32_t k_tap16[16] = {
    0xFF3CF4, 0x01A200, 0xFD019B, 0x051074, 0xF7B8BF, 0x0DBEE3, 0xE66E91, 0x50F1EB,
    0x50F1EB, 0xE66E91, 0x0DBEE3, 0xF7B8BF, 0x051074, 0xFD019B, 0x01A200, 0xFF3CF4,
};

static const uint32_t k_tap6[6] = {0x02B57B, 0xF07752, 0x4CD3AE, 0x4CD3AE, 0xF07752, 0x02B57B};

static const uint32_t k_lfo[32] = {
    0x19999A, 0x30A3D7, 0x428F5C, 0x547AE1, 0x6147AE, 0x68F5C3, 0x70A3D7, 0x770A3D,
    0x7AE148, 0x7D70A4, 0x7851EC, 0x6F5C29, 0x600000, 0x4B851F, 0x347AE1, 0x1AE148,
    0xE66666, 0xCF5C29, 0xBD70A4, 0xAB851F, 0x9EB852, 0x970A3D, 0x8F5C29, 0x88F5C3,
    0x851EB8, 0x828F5C, 0x87AE14, 0x90A3D7, 0xA00000, 0xB47AE1, 0xCB851F, 0xE51EB8,
};

static int64_t word_acc(int32_t value) { return (int64_t)value * 0x1000000; }

static int64_t set_a1(int64_t acc, int32_t value) {
  return (acc & ~((int64_t)0xFFFFFF << 24)) | ((int64_t)(value & 0xFFFFFF) << 24);
}

static int64_t set_a0(int64_t acc, int32_t value) {
  return (acc & ~(int64_t)0xFFFFFF) | (int64_t)(value & 0xFFFFFF);
}

static int64_t set_a2(int64_t acc, int32_t value) {
  return (acc & ~((int64_t)0xFF << 48)) | ((int64_t)(value & 0xFF) << 48);
}

static int64_t lsl_a1(int64_t acc) {
  const uint64_t u = (uint64_t)acc & 0xFFFFFFFFFFFFFFULL;
  const uint64_t a1 = ((u >> 24) & 0xFFFFFFu) << 1;
  return ifx_acc56((int64_t)((u & 0xFF000000FFFFFFULL) | ((a1 & 0xFFFFFFu) << 24)));
}

static int32_t store_a1(int64_t acc) { return ifx_s24((uint32_t)ifx_a1(acc)); }

static int32_t store_a0(int64_t acc) { return ifx_s24((uint32_t)ifx_a0(acc)); }

static int64_t mask_a1(int64_t acc, int32_t mask) { return ifx_and_a1(acc, mask); }

void ifx_chorus_core_state_init(ifx_chorus_core_state *state, int32_t regen_l, int32_t regen_r) {
  for (unsigned i = 0; i < 0x10000u; i++) {
    state->memory[i] = 0;
  }
  for (unsigned i = 0; i < 64u; i++) {
    state->history[i] = 0;
    state->sweep[i] = 0;
  }
  for (unsigned voice = 0; voice < IFX_CHORUS_VOICES; voice++) {
    state->phase[voice][0] = PHASE_STEP;
    state->phase[voice][1] = 0;
    state->phase[voice][2] = 0;
    state->phase[voice][3] = 0;
  }
  state->history_write = HISTORY_BASE;
  state->filter_write = FILTER_WRITE;
  state->filter_read = FILTER_READ;
  state->filter2_write = FILTER2_WRITE;
  state->read_l = LINE_L;
  state->read_r = LINE_R;
  state->regen_l = regen_l;
  state->regen_r = regen_r;
  state->staged_regen_l = regen_l;
  state->staged_regen_r = regen_r;
}

void ifx_chorus_core_process(ifx_chorus_core_state *state, const int32_t *voices, int32_t regen_l,
                             int32_t regen_r, const int32_t input[32], int32_t out_l[32],
                             int32_t out_r[32]) {
  int32_t *const memory = state->memory;
  int32_t *const history = state->history;
  int32_t *const sweep = state->sweep;

  if (state->staged_regen_l != regen_l) {
    state->staged_regen_l = regen_l;
    state->regen_l = regen_l;
  }
  if (state->staged_regen_r != regen_r) {
    state->staged_regen_r = regen_r;
    state->regen_r = regen_r;
  }

  /* P:$00DC: the regeneration limiter over the eight voices' pan gains. */
  {
    int64_t a = 0;
    const int32_t x0 = state->regen_l;
    const int32_t x1 = state->regen_r;
    for (unsigned voice = 0; voice < IFX_CHORUS_VOICES; voice++) {
      a = ifx_mac(a, voices[voice * 8u + 6u], x0);
      a = ifx_mac(a, x1, voices[voice * 8u + 7u]);
    }
    if (ifx_es(a)) {
      const int32_t y0 = LIMIT_COEF;
      a = ifx_mpyr(y0, x0);
      state->regen_l = ifx_store24(a);
      a = ifx_mpyr(x1, y0);
      state->regen_r = ifx_store24(a);
    }
  }

  /* P:$00F0: input plus the regeneration sum into X:$40..$5F. */
  {
    uint32_t r1 = state->history_write;
    const uint32_t m1 = 0x3Fu;
    uint32_t r5 = state->read_l;
    uint32_t r6 = state->read_r;
    const uint32_t m5 = 0x3FFFu;
    const uint32_t m6 = 0x3FFFu;
    const int32_t x0 = state->regen_l;
    const int32_t x1 = state->regen_r;
    unsigned sample = 0;
    int32_t y0 = memory[r5];
    r5 = ifx_agu_step(r5, 1, m5);
    int64_t b = word_acc(input[sample++]);
    int32_t y1 = memory[r6];
    r6 = ifx_agu_step(r6, 1, m6);
    b = ifx_mac(b, y0, x0);
    y0 = memory[r5];
    r5 = ifx_agu_step(r5, 1, m5);
    b = ifx_macr(b, y1, x1);
    int64_t a = word_acc(input[sample++]);
    y1 = memory[r6];
    r6 = ifx_agu_step(r6, 1, m6);
    for (unsigned i = 0; i < 15u; i++) {
      a = ifx_mac(a, y0, x0);
      history[r1 & 0x3Fu] = ifx_store24(b);
      r1 = ifx_agu_step(r1, 1, m1);
      y0 = memory[r5];
      r5 = ifx_agu_step(r5, 1, m5);
      a = ifx_macr(a, y1, x1);
      b = word_acc(input[sample++]);
      y1 = memory[r6];
      r6 = ifx_agu_step(r6, 1, m6);
      b = ifx_mac(b, y0, x0);
      history[r1 & 0x3Fu] = ifx_store24(a);
      r1 = ifx_agu_step(r1, 1, m1);
      y0 = memory[r5];
      r5 = ifx_agu_step(r5, 1, m5);
      b = ifx_macr(b, y1, x1);
      a = word_acc(input[sample++]);
      y1 = memory[r6];
      r6 = ifx_agu_step(r6, 1, m6);
    }
    a = ifx_mac(a, y0, x0);
    history[r1 & 0x3Fu] = ifx_store24(b);
    r1 = ifx_agu_step(r1, 1, m1);
    a = ifx_macr(a, y1, x1);
    history[r1 & 0x3Fu] = ifx_store24(a);

    /* P:$010A: the 16-tap filter into the Y:$2000-history at stride two. */
    r1 = state->history_write;
    uint32_t r4 = TAP_BASE;
    const uint32_t m4 = 0x0Fu;
    r5 = state->filter_write;
    const uint32_t fm5 = 0x1FFFu;
    const uint32_t n5 = 2u;
    for (unsigned out = 0; out < 32u; out++) {
      a = 0;
      int32_t tap_word = history[r1 & 0x3Fu];
      r1 = ifx_agu_step(r1, -1, m1);
      y0 = ifx_s24(k_tap16[r4 & 0x0Fu]);
      r4 = ifx_agu_step(r4, 1, m4);
      for (unsigned tap = 0; tap < 15u; tap++) {
        a = ifx_mac(a, y0, tap_word);
        tap_word = history[r1 & 0x3Fu];
        r1 = ifx_agu_step(r1, -1, m1);
        y0 = ifx_s24(k_tap16[r4 & 0x0Fu]);
        r4 = ifx_agu_step(r4, 1, m4);
      }
      a = ifx_macr(a, y0, tap_word);
      r1 = ifx_agu_step(r1, 8, m1);
      y1 = history[r1 & 0x3Fu];
      r1 = ifx_agu_step(r1, 8, m1);
      memory[r5 & 0xFFFFu] = y1;
      r5 = ifx_agu_step(r5, (int32_t)n5, fm5);
      memory[r5 & 0xFFFFu] = ifx_store24(a);
      r5 = ifx_agu_step(r5, (int32_t)n5, fm5);
      r1 = ifx_agu_step(r1, 1, m1);
    }
    state->filter_write = r5;
    state->history_write = r1;

    /* P:$011E: the 6-tap filter into the Y:$2000-history. */
    uint32_t r3 = state->filter_read;
    const uint32_t m3 = 0x1FFFu;
    const uint32_t n3 = 2u;
    uint32_t fr4 = r3;
    const uint32_t fm4 = m3;
    const uint32_t fn4 = n3;
    uint32_t fr1 = TAP6_BASE;
    const uint32_t fm1 = 0x05u;
    uint32_t fr5 = state->filter2_write;
    const uint32_t f2m5 = 0x1FFFu;
    const uint32_t fn5 = 2u;
    for (unsigned out = 0; out < 64u; out++) {
      a = 0;
      int32_t tap_word = ifx_s24(k_tap6[fr1 - TAP6_BASE]);
      fr1 = ifx_agu_step(fr1, 1, fm1);
      y0 = memory[fr4 & 0xFFFFu];
      fr4 = ifx_agu_step(fr4, (int32_t)fn4, fm4);
      for (unsigned tap = 0; tap < 5u; tap++) {
        a = ifx_mac(a, y0, tap_word);
        tap_word = ifx_s24(k_tap6[fr1 - TAP6_BASE]);
        fr1 = ifx_agu_step(fr1, 1, fm1);
        y0 = memory[fr4 & 0xFFFFu];
        fr4 = ifx_agu_step(fr4, (int32_t)fn4, fm4);
      }
      a = ifx_macr(a, y0, tap_word);
      r3 = ifx_agu_step(r3, (int32_t)n3, m3);
      fr4 = r3;
      memory[fr5 & 0xFFFFu] = ifx_store24(a);
      fr5 = ifx_agu_step(fr5, (int32_t)fn5, f2m5);
    }
    state->filter_read = r3;
    state->filter2_write = fr5;
  }

  /* P:$0132: the eight voices, each a phase sweep and an interpolated read. */
  {
    const uint32_t m4 = 0x3Fu;
    const uint32_t n5 = 0x2000u;
    const uint32_t m3 = 0x3FFFu;
    const uint32_t m6 = 0x3FFFu;

    for (unsigned voice = 0; voice < IFX_CHORUS_VOICES; voice++) {
      int32_t *const phase = state->phase[voice];
      uint32_t r4;
      uint32_t r5;
      uint32_t r3;
      uint32_t r6;
      int64_t a;
      int64_t b;
      int32_t x0;
      int32_t x1;
      int32_t y0;
      int32_t y1;
      /* Clamp the sweep accumulator's fractional state while it is small. */
      a = word_acc((int32_t)state->filter2_write);
      b = (int64_t)0x2000 << 24;
      a = ifx_sub(a, b);
      b = word_acc(phase[0]);
      a = ifx_sub(a, b);

      b = (int64_t)0x0200 << 24;
      if (ifx_abs56(a) < ifx_abs56(b)) {
        b = 0;
        a = ifx_add(a, b);
        if (a > 0) {
          b = (int64_t)0x10 << 24;
        }
        phase[2] = ifx_s24((uint32_t)(b >> 24));
        phase[3] = ifx_s24((uint32_t)b);
      }

      /* Advance the LFO phase and interpolate the 32-entry table. */
      a = set_a1(0, phase[2]);
      a = set_a0(a, phase[3]);
      x0 = voices[voice * 8u + 0u];
      a = ifx_acc56(a + (int64_t)(uint32_t)x0);
      y1 = 0x1F;
      a = mask_a1(a, y1);
      phase[2] = store_a1(a);
      phase[3] = store_a0(a);
      r4 = (uint32_t)ifx_a1(a);
      a = lsl_a1(a);
      a = ifx_asr(a);
      x1 = store_a0(a);
      r4 = ifx_agu_step(r4, 0x1800, 0x1Fu);
      a = word_acc(ifx_s24(k_lfo[r4 & 0x1Fu]));
      r4 = ifx_agu_step(r4, 1, 0x1Fu);
      x0 = store_a1(a);
      y1 = ifx_s24(k_lfo[r4 & 0x1Fu]);
      a = ifx_mac(a, -x1, x0);
      a = ifx_macr(a, y1, x1);
      x0 = voices[voice * 8u + 1u];
      y0 = store_a1(a);
      b = ifx_mpyr(y0, x0);
      a = 0;
      a = set_a2(a, 4);
      a = ifx_add(a, b);
      b = set_a0(0, ifx_a1(a));
      b = set_a1(b, ifx_a2(a));

      /* Build the 32-entry sweep of read pointers; the AGU delay makes each
       * a1/a0 pair adjacent and advances the table by two words. */
      r4 = SWEEP_BASE;
      y1 = 0x1FFF;
      a = set_a1(0, phase[0]);
      a = set_a0(a, phase[1]);
      a = ifx_acc56(a + b);
      a = mask_a1(a, y1);
      for (unsigned index = 0; index < 31u; index++) {
        sweep[r4 & 0x3Fu] = store_a1(a);
        sweep[(r4 + 1u) & 0x3Fu] = store_a0(a);
        r4 = ifx_agu_step(r4, 2, m4);
        a = ifx_acc56(a + b);
        a = mask_a1(a, y1);
      }
      sweep[r4 & 0x3Fu] = store_a1(a);
      sweep[(r4 + 1u) & 0x3Fu] = store_a0(a);
      r4 = ifx_agu_step(r4, 2, m4);
      phase[0] = store_a1(a);
      phase[1] = store_a0(a);

      /* P:$017F: interpolate the 32 swept taps. */
      b = 0;
      r5 = (uint32_t)sweep[r4 & 0x3Fu];
      r4 = ifx_agu_step(r4, 1, m4);
      b = set_a1(b, sweep[r4 & 0x3Fu]);
      r4 = ifx_agu_step(r4, 1, m4);
      for (unsigned tap = 0; tap < 32u; tap++) {
        b = (b & ~((int64_t)0xFFFFFF << 24)) |
            ((int64_t)(((uint32_t)(b >> 24) & 0xFFFFFFu) >> 1) << 24);
        r5 = ifx_agu_step(r5, (int32_t)n5, 0x1FFFu);
        x1 = store_a1(b);
        a = word_acc(memory[r5 & 0xFFFFu]);
        r5 = ifx_agu_step(r5, 1, 0x1FFFu);
        x0 = store_a1(a);
        y1 = memory[r5 & 0xFFFFu];
        a = ifx_mac(a, -x1, x0);
        a = ifx_macr(a, y1, x1);
        r5 = (uint32_t)sweep[r4 & 0x3Fu];
        r4 = ifx_agu_step(r4, 1, m4);
        b = set_a1(0, sweep[r4 & 0x3Fu]);
        sweep[r4 & 0x3Fu] = ifx_store24(a);
        r4 = ifx_agu_step(r4, 1, m4);
      }

      /* P:$018C: add the voice into both delay lines at its delay. */
      r3 = state->read_l;
      {
        const uint32_t delay = (uint32_t)voices[voice * 8u + 3u] & 0xFFFFu;
        r3 = ifx_agu_step(r3, (int32_t)delay, m3);
        r6 = state->read_r;
        r6 = ifx_agu_step(r6, (int32_t)delay, m6);
      }
      r4 = ifx_agu_step(r4, 1, m4);
      x1 = voices[voice * 8u + 6u];
      y1 = voices[voice * 8u + 7u];
      for (unsigned tap = 0; tap < 32u; tap++) {
        a = word_acc(memory[r3 & 0xFFFFu]);
        y0 = sweep[r4 & 0x3Fu];
        r4 = ifx_agu_step(r4, 2, m4);
        a = ifx_macr(a, x1, y0);
        b = word_acc(memory[r6 & 0xFFFFu]);
        b = ifx_macr(b, y1, y0);
        memory[r3 & 0xFFFFu] = ifx_store24(a);
        r3 = ifx_agu_step(r3, 1, m3);
        memory[r6 & 0xFFFFu] = ifx_store24(b);
        r6 = ifx_agu_step(r6, 1, m6);
      }
    }
  }

  /* P:$01A1: read and clear the oldest block of both lines. */
  {
    uint32_t r3 = state->read_l;
    uint32_t r4 = state->read_r;
    const uint32_t m3 = 0x3FFFu;
    const uint32_t m4 = 0x3FFFu;
    for (unsigned frame = 0; frame < 32u; frame++) {
      const int32_t left = memory[r3 & 0xFFFFu];
      memory[r3 & 0xFFFFu] = 0;
      r3 = ifx_agu_step(r3, 1, m3);
      out_l[frame] = left;
      const int32_t right = memory[r4 & 0xFFFFu];
      memory[r4 & 0xFFFFu] = 0;
      r4 = ifx_agu_step(r4, 1, m4);
      out_r[frame] = right;
    }
    state->read_l = r3;
    state->read_r = r4;
  }
}
