#include "ifx_pitch_alt.h"

#include "ifx_alt_process.h"

static uint32_t hold(const ifx_pitch_config *from, const ifx_pitch_config *to) {
  return ifx_alt_core_voices(&from->coef[IFX_PITCH_COEF_VOICES], &to->coef[IFX_PITCH_COEF_VOICES],
                             4u);
}

IFX_ALT_PROCESS_DEFINE(pitch, hold)
