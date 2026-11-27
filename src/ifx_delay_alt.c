#include "ifx_delay_alt.h"

#include "ifx_alt_process.h"
#include "ifx_wipe_core.h"

enum {
  TYPE_FLAGS = IFX_DELAY_FLAG_STEREO | IFX_DELAY_FLAG_PINGPONG | IFX_DELAY_FLAG_2TAP,
};

/* A config's longest tap in samples; 2-Tap's delay words count pairs. */
static int32_t longest_tap(const ifx_delay_config *config) {
  const int32_t *const coef = config->coef;
  const int32_t tap = coef[IFX_DELAY_COEF_TIME1] > coef[IFX_DELAY_COEF_TIME2]
                          ? coef[IFX_DELAY_COEF_TIME1]
                          : coef[IFX_DELAY_COEF_TIME2];
  return (coef[IFX_DELAY_COEF_DELAY_FLAGS] & IFX_DELAY_FLAG_2TAP) != 0 ? 2 * tap : tap;
}

static uint32_t hold(const ifx_delay_config *from, const ifx_delay_config *to) {
  const int32_t *const a = from->coef;
  const int32_t *const b = to->coef;
  if (((a[IFX_DELAY_COEF_DELAY_FLAGS] ^ b[IFX_DELAY_COEF_DELAY_FLAGS]) & TYPE_FLAGS) != 0) {
    return IFX_WIPE_CORE_BLOCKS + ifx_alt_core_blocks(longest_tap(to));
  }
  if (a[IFX_DELAY_COEF_DELAY_FLAGS] != b[IFX_DELAY_COEF_DELAY_FLAGS]) {
    const int32_t from_tap = longest_tap(from);
    const int32_t to_tap = longest_tap(to);
    return ifx_alt_core_blocks(from_tap > to_tap ? from_tap : to_tap);
  }
  return 0;
}

IFX_ALT_PROCESS_DEFINE(delay, hold)
