#include "ifx_wipe_core.h"

enum {
  WIPE_STEP = 0x800,
  WIPE_END = 0x1000,
};

void ifx_wipe_core_init(ifx_wipe_core *wipe) {
  wipe->pointer = 0;
  wipe->active = 0;
  wipe->init_pending = 0;
  wipe->skip_pass = 0;
}

void ifx_wipe_core_restart(ifx_wipe_core *wipe) {
  wipe->pointer = 0xFFFFu;
  wipe->active = 1;
  wipe->init_pending = 1;
}

void ifx_wipe_core_call(ifx_wipe_core *wipe, int32_t memory[0x10000], int32_t wet_left[32],
                        int32_t wet_right[32]) {
  for (unsigned i = 0; i < 32u; i++) {
    wet_left[i] = 0;
    wet_right[i] = 0;
  }
  if (wipe->pointer > WIPE_END) {
    const uint32_t first = wipe->pointer - (WIPE_STEP - 1u);
    for (uint32_t address = first; address <= wipe->pointer; address++) {
      memory[address & 0xFFFFu] = 0;
    }
    wipe->pointer -= WIPE_STEP;
    return;
  }
  if (wipe->init_pending != 0) {
    wipe->active = 0;
    wipe->skip_pass = 1;
  }
}
