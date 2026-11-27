#include "ifx_regen_core.h"

#include <stddef.h>

#include "ifx_dsp_math.h"

enum {
  LIMIT_COEF = 0x71EB85, /* 0.8889 */
  HISTORY_M = IFX_REGEN_CORE_HISTORY_WORDS - 1u,
};

static void scale(ifx_regen_core_state *state) {
  state->left = ifx_store24(ifx_mpyr(LIMIT_COEF, state->left));
  state->right = ifx_store24(ifx_mpyr(state->right, LIMIT_COEF));
}

void ifx_regen_core_state_init(ifx_regen_core_state *state, int32_t left, int32_t right) {
  state->left = left;
  state->right = right;
  state->staged_left = left;
  state->staged_right = right;
}

void ifx_regen_core_stage(ifx_regen_core_state *state, int32_t left, int32_t right) {
  if (state->staged_left != left) {
    state->staged_left = left;
    state->left = left;
  }
  if (state->staged_right != right) {
    state->staged_right = right;
    state->right = right;
  }
}

void ifx_regen_core_limit(ifx_regen_core_state *state) {
  if (ifx_es(ifx_add24(ifx_word_acc(state->left), state->right))) {
    scale(state);
  }
}

void ifx_regen_core_limit_voices(ifx_regen_core_state *state, const int32_t *voices,
                                 unsigned count) {
  int64_t a = 0;
  for (unsigned voice = 0; voice < count; voice++) {
    a = ifx_mac(a, voices[voice * 8u + 6u], state->left);
    a = ifx_mac(a, state->right, voices[voice * 8u + 7u]);
  }
  if (ifx_es(a)) {
    scale(state);
  }
}

/* Software-pipelined as the firmware runs it: each sample's two line words are
 * read one step ahead of the MACs that use them. */
void ifx_regen_core_feed(const ifx_regen_core_state *state, const int32_t memory[0x10000],
                         uint32_t read_l, uint32_t read_r, const int32_t input[IFX_BLOCK_FRAMES],
                         int32_t history[IFX_REGEN_CORE_HISTORY_WORDS], uint32_t history_write) {
  const int32_t x0 = state->left;
  const int32_t x1 = state->right;
  uint32_t r1 = history_write;
  uint32_t r5 = read_l;
  uint32_t r6 = read_r;
  unsigned sample = 0;
  int32_t y0 = memory[r5];
  r5 = ifx_agu_step(r5, 1, IFX_REGEN_CORE_LINE_M);
  int64_t b = ifx_word_acc(input[sample++]);
  int32_t y1 = memory[r6];
  r6 = ifx_agu_step(r6, 1, IFX_REGEN_CORE_LINE_M);
  b = ifx_mac(b, y0, x0);
  y0 = memory[r5];
  r5 = ifx_agu_step(r5, 1, IFX_REGEN_CORE_LINE_M);
  b = ifx_macr(b, y1, x1);
  int64_t a = ifx_word_acc(input[sample++]);
  y1 = memory[r6];
  r6 = ifx_agu_step(r6, 1, IFX_REGEN_CORE_LINE_M);
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES / 2u - 1u; i++) {
    a = ifx_mac(a, y0, x0);
    history[r1 & HISTORY_M] = ifx_store24(b);
    r1 = ifx_agu_step(r1, 1, HISTORY_M);
    y0 = memory[r5];
    r5 = ifx_agu_step(r5, 1, IFX_REGEN_CORE_LINE_M);
    a = ifx_macr(a, y1, x1);
    b = ifx_word_acc(input[sample++]);
    y1 = memory[r6];
    r6 = ifx_agu_step(r6, 1, IFX_REGEN_CORE_LINE_M);
    b = ifx_mac(b, y0, x0);
    history[r1 & HISTORY_M] = ifx_store24(a);
    r1 = ifx_agu_step(r1, 1, HISTORY_M);
    y0 = memory[r5];
    r5 = ifx_agu_step(r5, 1, IFX_REGEN_CORE_LINE_M);
    b = ifx_macr(b, y1, x1);
    a = ifx_word_acc(input[sample++]);
    y1 = memory[r6];
    r6 = ifx_agu_step(r6, 1, IFX_REGEN_CORE_LINE_M);
  }
  a = ifx_mac(a, y0, x0);
  history[r1 & HISTORY_M] = ifx_store24(b);
  r1 = ifx_agu_step(r1, 1, HISTORY_M);
  a = ifx_macr(a, y1, x1);
  history[r1 & HISTORY_M] = ifx_store24(a);
}

void ifx_regen_core_add(int32_t memory[0x10000], int32_t *bus, uint32_t read_l, uint32_t read_r,
                        int32_t delay, int32_t gain_l, int32_t gain_r,
                        const int32_t voice[IFX_BLOCK_FRAMES]) {
  if (bus != NULL && delay == 0) {
    for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
      bus[i] = ifx_store24(ifx_macr(ifx_word_acc(bus[i]), gain_l, voice[i]));
      bus[IFX_BLOCK_FRAMES + i] =
          ifx_store24(ifx_macr(ifx_word_acc(bus[IFX_BLOCK_FRAMES + i]), gain_r, voice[i]));
    }
    return;
  }
  uint32_t r_l = ifx_agu_step(read_l, delay, IFX_REGEN_CORE_LINE_M);
  uint32_t r_r = ifx_agu_step(read_r, delay, IFX_REGEN_CORE_LINE_M);
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    memory[r_l] = ifx_store24(ifx_macr(ifx_word_acc(memory[r_l]), gain_l, voice[i]));
    r_l = ifx_agu_step(r_l, 1, IFX_REGEN_CORE_LINE_M);
    memory[r_r] = ifx_store24(ifx_macr(ifx_word_acc(memory[r_r]), gain_r, voice[i]));
    r_r = ifx_agu_step(r_r, 1, IFX_REGEN_CORE_LINE_M);
  }
}

void ifx_regen_core_drain(int32_t memory[0x10000], uint32_t *read_l, uint32_t *read_r,
                          int32_t out_l[IFX_BLOCK_FRAMES], int32_t out_r[IFX_BLOCK_FRAMES]) {
  uint32_t r3 = *read_l;
  uint32_t r4 = *read_r;
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    out_l[i] = memory[r3];
    memory[r3] = 0;
    r3 = ifx_agu_step(r3, 1, IFX_REGEN_CORE_LINE_M);
    out_r[i] = memory[r4];
    memory[r4] = 0;
    r4 = ifx_agu_step(r4, 1, IFX_REGEN_CORE_LINE_M);
  }
  *read_l = r3;
  *read_r = r4;
}
