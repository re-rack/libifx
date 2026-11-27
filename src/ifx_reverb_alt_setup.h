/* The de-popped Reverb's setup, which is the effect's own
 * (ifx_reverb_setup.h). */
#ifndef IFX_REVERB_ALT_SETUP_H
#define IFX_REVERB_ALT_SETUP_H

#include "ifx_reverb_alt.h"
#include "ifx_reverb_setup.h"

typedef ifx_reverb_setup_raw ifx_reverb_alt_setup_raw;

int ifx_reverb_alt_setup_config(const ifx_reverb_alt_setup_raw *raw, ifx_reverb_alt_config *config);

#endif /* IFX_REVERB_ALT_SETUP_H */
