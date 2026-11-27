#include "ifx_pitch_reverb_alt.h"

#include "ifx_alt_process.h"

static uint32_t hold(const ifx_pitch_reverb_config *from, const ifx_pitch_reverb_config *to) {
  return ifx_alt_core_voices(&from->coef[IFX_PITCH_REVERB_COEF_VOICES],
                             &to->coef[IFX_PITCH_REVERB_COEF_VOICES], 3u);
}

IFX_ALT_PROCESS_DEFINE(pitch_reverb, hold)
