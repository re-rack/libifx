#include "ifx_wipe_core.h"

enum {
  WIPE_START = 0xFFFF,
  WIPE_STEP = 0x800,
  WIPE_END = 0x1000,
};

/* The clearing passes, the pass that still skips, then the initialising one. */
_Static_assert((WIPE_START - WIPE_END - 1) / WIPE_STEP + 1 + 2 == IFX_WIPE_CORE_BLOCKS,
               "the wipe's length");

void ifx_wipe_core_state_init(ifx_wipe_core_state *state) {
  state->pointer = 0;
  state->active = 0;
  state->init_pending = 0;
  state->skip_pass = 0;
}

void ifx_wipe_core_restart(ifx_wipe_core_state *state) {
  if (state->init_pending != 0) {
    return;
  }
  state->pointer = WIPE_START;
  state->active = 1;
  state->init_pending = 1;
}

void ifx_wipe_core_pass(ifx_wipe_core_state *state, int32_t memory[0x10000],
                        int32_t wet_left[IFX_BLOCK_FRAMES], int32_t wet_right[IFX_BLOCK_FRAMES]) {
  if (state->active == 0) {
    return;
  }
  for (unsigned i = 0; i < IFX_BLOCK_FRAMES; i++) {
    wet_left[i] = 0;
    wet_right[i] = 0;
  }
  if (state->pointer > WIPE_END) {
    for (uint32_t address = state->pointer - (WIPE_STEP - 1u); address <= state->pointer;
         address++) {
      memory[address] = 0;
    }
    state->pointer -= WIPE_STEP;
    return;
  }
  if (state->init_pending != 0) {
    state->active = 0;
    state->skip_pass = 1;
  }
}

ifx_wipe_core_step ifx_wipe_core_next(ifx_wipe_core_state *state) {
  if (state->skip_pass != 0) {
    state->skip_pass = 0;
    return IFX_WIPE_CORE_SKIP;
  }
  if (state->active != 0) {
    return IFX_WIPE_CORE_SKIP;
  }
  if (state->init_pending != 0) {
    state->init_pending = 0;
    return IFX_WIPE_CORE_INIT;
  }
  return IFX_WIPE_CORE_RUN;
}
