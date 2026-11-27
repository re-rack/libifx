/* The de-popped Delay's setup, which is the effect's own
 * (ifx_delay_setup.h). */
#ifndef IFX_DELAY_ALT_SETUP_H
#define IFX_DELAY_ALT_SETUP_H

#include "ifx_delay_alt.h"
#include "ifx_delay_setup.h"

typedef ifx_delay_setup_raw ifx_delay_alt_setup_raw;

int ifx_delay_alt_setup_config(const ifx_delay_alt_setup_raw *raw, ifx_delay_alt_config *config);

#endif /* IFX_DELAY_ALT_SETUP_H */
