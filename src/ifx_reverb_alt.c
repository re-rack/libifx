#include "ifx_reverb_alt.h"

#include "ifx_alt_process.h"
#include "ifx_wipe_core.h"

static uint32_t hold(const ifx_reverb_config *from, const ifx_reverb_config *to) {
  if (from->coef[IFX_REVERB_COEF_TYPE] == to->coef[IFX_REVERB_COEF_TYPE]) {
    return 0;
  }
  const int32_t left = to->coef[IFX_REVERB_COEF_PREDELAY_LEFT];
  const int32_t right = to->coef[IFX_REVERB_COEF_PREDELAY_RIGHT];
  return IFX_WIPE_CORE_BLOCKS + ifx_alt_core_blocks(left > right ? left : right);
}

IFX_ALT_PROCESS_DEFINE(reverb, hold)
