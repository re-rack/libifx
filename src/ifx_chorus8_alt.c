#include "ifx_chorus8_alt.h"

#include "ifx_alt_process.h"

static uint32_t hold(const ifx_chorus8_config *from, const ifx_chorus8_config *to) {
  return ifx_alt_core_voices(&from->coef[IFX_CHORUS8_COEF_VOICES],
                             &to->coef[IFX_CHORUS8_COEF_VOICES], 8u);
}

IFX_ALT_PROCESS_DEFINE(chorus8, hold)
