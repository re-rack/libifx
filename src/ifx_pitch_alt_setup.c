#include "ifx_pitch_alt_setup.h"

int ifx_pitch_alt_setup_config(const ifx_pitch_alt_setup_raw *raw, ifx_pitch_alt_config *config) {
  return ifx_pitch_setup_config(raw, config);
}
