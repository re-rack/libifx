#include "ifx_chorus8_alt_setup.h"

int ifx_chorus8_alt_setup_config(const ifx_chorus8_alt_setup_raw *raw,
                                 ifx_chorus8_alt_config *config) {
  return ifx_chorus8_setup_config(raw, config);
}
