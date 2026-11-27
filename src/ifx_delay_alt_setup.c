#include "ifx_delay_alt_setup.h"

int ifx_delay_alt_setup_config(const ifx_delay_alt_setup_raw *raw, ifx_delay_alt_config *config) {
  return ifx_delay_setup_config(raw, config);
}
