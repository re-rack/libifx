#include "ifx_chorus_reverb_alt_setup.h"

int ifx_chorus_reverb_alt_setup_config(const ifx_chorus_reverb_alt_setup_raw *raw,
                                       ifx_chorus_reverb_alt_config *config) {
  return ifx_chorus_reverb_setup_config(raw, config);
}
