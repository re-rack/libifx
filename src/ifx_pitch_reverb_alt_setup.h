/* The de-popped Pitch Shift, Delay and Reverb's setup, which is the effect's own
 * (ifx_pitch_reverb_setup.h). */
#ifndef IFX_PITCH_REVERB_ALT_SETUP_H
#define IFX_PITCH_REVERB_ALT_SETUP_H

#include "ifx_pitch_reverb_alt.h"
#include "ifx_pitch_reverb_setup.h"

typedef ifx_pitch_reverb_setup_raw ifx_pitch_reverb_alt_setup_raw;

int ifx_pitch_reverb_alt_setup_config(const ifx_pitch_reverb_alt_setup_raw *raw,
                                      ifx_pitch_reverb_alt_config *config);

#endif /* IFX_PITCH_REVERB_ALT_SETUP_H */
