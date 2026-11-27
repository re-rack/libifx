#include "ifx_reverb_alt_setup.h"

int ifx_reverb_alt_setup_config(const ifx_reverb_alt_setup_raw *raw,
                                ifx_reverb_alt_config *config) {
  return ifx_reverb_setup_config(raw, config);
}
