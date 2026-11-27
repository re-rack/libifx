#include "ifx_alt_core.h"

#include "ifx_base.h"
#include "ifx_fade.h"

enum { VOICE_WORDS = 8, VOICE_DELAY = 3 };

void ifx_alt_core_state_init(ifx_alt_core_state *state) {
  state->from = IFX_ALT_CORE_NONE;
  state->to = IFX_ALT_CORE_EXACT;
  state->pos = 0;
  state->wait = 0;
}

unsigned ifx_alt_core_turn(ifx_alt_core_state *state, uint32_t hold) {
  unsigned freeze = IFX_ALT_CORE_NONE;
  if (state->to == IFX_ALT_CORE_EXACT) {
    freeze = state->from == 1u ? 2u : 1u;
    state->to = freeze;
  }
  if (hold > state->wait) {
    state->wait = hold;
  }
  return freeze;
}

int ifx_alt_core_runs(const ifx_alt_core_state *state, unsigned source) {
  return source == IFX_ALT_CORE_EXACT || source == state->from || source == state->to;
}

void ifx_alt_core_mix(ifx_alt_core_state *state, const int32_t *const left[IFX_ALT_CORE_SOURCES],
                      const int32_t *const right[IFX_ALT_CORE_SOURCES], int32_t *out_left,
                      int32_t *out_right, size_t frames) {
  for (size_t i = 0; i < frames; i++) {
    const int32_t to_left = left[state->to][i];
    const int32_t to_right = right[state->to][i];
    if (state->from == IFX_ALT_CORE_NONE) {
      out_left[i] = to_left;
      out_right[i] = to_right;
      continue;
    }
    const int64_t share = ifx_fade_share(++state->pos);
    out_left[i] = ifx_fade_blend(left[state->from][i], to_left, share);
    out_right[i] = ifx_fade_blend(right[state->from][i], to_right, share);
    if (state->pos == IFX_FADE_FRAMES) {
      state->from = IFX_ALT_CORE_NONE;
    }
  }
}

void ifx_alt_core_block(ifx_alt_core_state *state) {
  if (state->wait != 0u) {
    state->wait--;
    return;
  }
  if (state->from == IFX_ALT_CORE_NONE && state->to != IFX_ALT_CORE_EXACT) {
    state->from = state->to;
    state->to = IFX_ALT_CORE_EXACT;
    state->pos = 0;
  }
}

uint32_t ifx_alt_core_blocks(int32_t samples) {
  return ((uint32_t)samples + IFX_BLOCK_FRAMES - 1u) / IFX_BLOCK_FRAMES + 2u;
}

uint32_t ifx_alt_core_voices(const int32_t *from, const int32_t *to, unsigned voices) {
  uint32_t hold = 0;
  for (unsigned voice = 0; voice < voices; voice++) {
    const int32_t *const a = &from[VOICE_WORDS * voice];
    const int32_t *const b = &to[VOICE_WORDS * voice];
    int turned = 0;
    for (unsigned word = VOICE_DELAY; word < VOICE_WORDS; word++) {
      turned |= a[word] != b[word];
    }
    if (turned) {
      const uint32_t voice_hold =
          ifx_alt_core_blocks(a[VOICE_DELAY] > b[VOICE_DELAY] ? a[VOICE_DELAY] : b[VOICE_DELAY]);
      hold = voice_hold > hold ? voice_hold : hold;
    }
  }
  return hold;
}
