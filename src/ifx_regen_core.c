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
  const uint32_t mask = IFX_REGEN_CORE_LINE_M;
  const uint32_t base5 = read_l & ~mask;
  const uint32_t base6 = read_r & ~mask;
  uint32_t off5 = read_l & mask;
  uint32_t off6 = read_r & mask;
  uint32_t h = history_write & HISTORY_M;
  unsigned sample = 0;
  int32_t y0 = memory[base5 + off5];
  off5 = (off5 + 1u) & mask;
  int64_t b = ifx_word_acc(input[sample++]);
  int32_t y1 = memory[base6 + off6];
  off6 = (off6 + 1u) & mask;
  b = ifx_mac(b, y0, x0);
  y0 = memory[base5 + off5];
  off5 = (off5 + 1u) & mask;
  b = ifx_macr(b, y1, x1);
  int64_t a = ifx_word_acc(input[sample++]);
  y1 = memory[base6 + off6];
  off6 = (off6 + 1u) & mask;
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES / 2u - 1u; i++) {
    a = ifx_mac(a, y0, x0);
    history[h] = ifx_store24(b);
    h = (h + 1u) & HISTORY_M;
    y0 = memory[base5 + off5];
    off5 = (off5 + 1u) & mask;
    a = ifx_macr(a, y1, x1);
    b = ifx_word_acc(input[sample++]);
    y1 = memory[base6 + off6];
    off6 = (off6 + 1u) & mask;
    b = ifx_mac(b, y0, x0);
    history[h] = ifx_store24(a);
    h = (h + 1u) & HISTORY_M;
    y0 = memory[base5 + off5];
    off5 = (off5 + 1u) & mask;
    b = ifx_macr(b, y1, x1);
    a = ifx_word_acc(input[sample++]);
    y1 = memory[base6 + off6];
    off6 = (off6 + 1u) & mask;
  }
  a = ifx_mac(a, y0, x0);
  history[h] = ifx_store24(b);
  h = (h + 1u) & HISTORY_M;
  a = ifx_macr(a, y1, x1);
  history[h] = ifx_store24(a);
}

void ifx_regen_core_add(int32_t memory[0x10000], int32_t *bus, uint32_t read_l, uint32_t read_r,
                        int32_t delay, int32_t gain_l, int32_t gain_r,
                        const int32_t voice[IFX_BLOCK_FRAMES]) {
  if (bus != NULL && delay == 0) {
    for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
      bus[i] = ifx_macr_store(bus[i], gain_l, voice[i]);
      bus[IFX_BLOCK_FRAMES + i] = ifx_macr_store(bus[IFX_BLOCK_FRAMES + i], gain_r, voice[i]);
    }
    return;
  }
  const uint32_t mask = IFX_REGEN_CORE_LINE_M;
  const uint32_t r_l = ifx_agu_step(read_l, delay, mask);
  const uint32_t r_r = ifx_agu_step(read_r, delay, mask);
  const uint32_t base_l = r_l & ~mask;
  const uint32_t base_r = r_r & ~mask;
  uint32_t off_l = r_l & mask;
  uint32_t off_r = r_r & mask;
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    memory[base_l + off_l] = ifx_macr_store(memory[base_l + off_l], gain_l, voice[i]);
    off_l = (off_l + 1u) & mask;
    memory[base_r + off_r] = ifx_macr_store(memory[base_r + off_r], gain_r, voice[i]);
    off_r = (off_r + 1u) & mask;
  }
}

void ifx_regen_core_drain(int32_t memory[0x10000], uint32_t *read_l, uint32_t *read_r,
                          int32_t out_l[IFX_BLOCK_FRAMES], int32_t out_r[IFX_BLOCK_FRAMES]) {
  const uint32_t mask = IFX_REGEN_CORE_LINE_M;
  const uint32_t base_l = *read_l & ~mask;
  const uint32_t base_r = *read_r & ~mask;
  uint32_t off_l = *read_l & mask;
  uint32_t off_r = *read_r & mask;
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    out_l[i] = memory[base_l + off_l];
    memory[base_l + off_l] = 0;
    off_l = (off_l + 1u) & mask;
    out_r[i] = memory[base_r + off_r];
    memory[base_r + off_r] = 0;
    off_r = (off_r + 1u) & mask;
  }
  *read_l = base_l + off_l;
  *read_r = base_r + off_r;
}
