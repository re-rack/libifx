#include "ifx_chorus_reverb_alt.h"

#include "ifx_alt_process.h"

static uint32_t hold(const ifx_chorus_reverb_config *from, const ifx_chorus_reverb_config *to) {
  return ifx_alt_core_voices(&from->coef[IFX_CHORUS_REVERB_COEF_VOICES],
                             &to->coef[IFX_CHORUS_REVERB_COEF_VOICES], 4u);
}

IFX_ALT_PROCESS_DEFINE(chorus_reverb, hold)
