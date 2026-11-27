/* The de-popped 8 Voice Chorus's setup, which is the effect's own
 * (ifx_chorus8_setup.h). */
#ifndef IFX_CHORUS8_ALT_SETUP_H
#define IFX_CHORUS8_ALT_SETUP_H

#include "ifx_chorus8_alt.h"
#include "ifx_chorus8_setup.h"

typedef ifx_chorus8_setup_raw ifx_chorus8_alt_setup_raw;

int ifx_chorus8_alt_setup_config(const ifx_chorus8_alt_setup_raw *raw,
                                 ifx_chorus8_alt_config *config);

#endif /* IFX_CHORUS8_ALT_SETUP_H */
