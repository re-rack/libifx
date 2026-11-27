/* The de-popped Chorus, Delay and Reverb's setup, which is the effect's own
 * (ifx_chorus_reverb_setup.h). */
#ifndef IFX_CHORUS_REVERB_ALT_SETUP_H
#define IFX_CHORUS_REVERB_ALT_SETUP_H

#include "ifx_chorus_reverb_alt.h"
#include "ifx_chorus_reverb_setup.h"

typedef ifx_chorus_reverb_setup_raw ifx_chorus_reverb_alt_setup_raw;

int ifx_chorus_reverb_alt_setup_config(const ifx_chorus_reverb_alt_setup_raw *raw,
                                       ifx_chorus_reverb_alt_config *config);

#endif /* IFX_CHORUS_REVERB_ALT_SETUP_H */
