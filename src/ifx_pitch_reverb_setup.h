/* Configuration 1's raw knobs and their conversion to the words its message
 * handlers stage. */
#ifndef IFX_PITCH_REVERB_SETUP_H
#define IFX_PITCH_REVERB_SETUP_H

#include "ifx_base.h"
#include "ifx_pitch_reverb.h"

typedef struct {
  int effects_level;      /* 0..58: EFFECTS LVL */
  int direct_left;        /* 0..58: L DIR LVL */
  int direct_right;       /* 0..58: R DIR LVL */
  int direct_hush;        /* 0..1: PRE or POST */
  int pitch_level;        /* 0..58: PITCH LVL */
  int delay_level;        /* 0..58: DELAY LVL */
  int reverb_level;       /* 0..58: REVERB LVL */
  int regen_left;         /* 0..58: REGEN L */
  int regen_right;        /* 0..58: REGEN R */
  int hush;               /* 0..1: HUSH I/O */
  int hush_threshold;     /* 0..24: EXP THRESH */
  int hush_release;       /* 0..9: REL RATE */
  int pitch1;             /* 0..180: PITCH 1, 20-cent steps */
  int fine1;              /* 0..40: FINE 1, 1-cent steps */
  int level1;             /* 0..58: LEVEL 1 */
  int pan1;               /* 0..100: PAN 1 */
  int delay1;             /* 0..209: DELAY 1, 2 ms steps */
  int pitch2;
  int fine2;
  int level2;
  int pan2;
  int delay2;
  int level3;             /* 0..58: LEVEL 3, the Delay 3 voice */
  int pan3;
  int delay3;
  int ducker;             /* 0..3: Off, Dly, Rev or Both */
  int ducker_sensitivity; /* 0..24: SENSITIVITY */
  int ducker_attenuation; /* 0..58: ATTENUATION */
  int ducker_release;     /* 0..19: RELEASE RATE */
  int rev_input;          /* 0..1: Muted or Active */
  int dir_in_pan;         /* 0..100: DIR IN PAN */
  int mix_dir_dly;        /* 0..100: MIX DIR/DLY */
  int reverb_decay;       /* 0..99: REV DECAY */
  int hf_damp;            /* 0..99: RV HF DAMP */
} ifx_pitch_reverb_setup_raw;

int ifx_pitch_reverb_setup_config(const ifx_context *context, const ifx_pitch_reverb_setup_raw *raw,
                                  ifx_pitch_reverb_config *config);

#endif /* IFX_PITCH_REVERB_SETUP_H */
