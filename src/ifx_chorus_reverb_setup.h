/* Configuration 0's raw knobs and their conversion to the words its message
 * handlers stage. */
#ifndef IFX_CHORUS_REVERB_SETUP_H
#define IFX_CHORUS_REVERB_SETUP_H

#include "ifx_base.h"
#include "ifx_chorus_reverb.h"

typedef struct {
  int effects_level;      /* 0..58: EFFECTS LVL */
  int direct_left;        /* 0..58: L DIR LVL */
  int direct_right;       /* 0..58: R DIR LVL */
  int direct_hush;        /* 0..1: PRE or POST */
  int analog_direct;      /* 0..1: the analog direct path off or on, heard at DIRECT HUSH PRE */
  int output_level;       /* 0..100: OUTPUT LEVEL, percent; 100 passes the output unchanged */
  int chorus_level;       /* 0..58: CHORUS LVL */
  int delay_level;        /* 0..58: DELAY LVL */
  int reverb_level;       /* 0..58: REVERB LVL */
  int regen_left;         /* 0..58: REGEN L */
  int regen_right;        /* 0..58: REGEN R */
  int hush;               /* 0..1: HUSH I/O */
  int hush_threshold;     /* 0..24: EXP THRESH */
  int hush_release;       /* 0..9: REL RATE */
  int level1;             /* 0..58: LEVEL 1 */
  int pan1;               /* 0..100: PAN 1 */
  int delay1;             /* 0..209: DELAY 1, 2 ms steps */
  int depth1;             /* 0..100: DEPTH 1 */
  int rate1;              /* 0..254: RATE 1 */
  int level2;
  int pan2;
  int delay2;
  int depth2;
  int rate2;
  int level3;
  int pan3;
  int delay3;
  int depth3;
  int rate3;
  int level4;
  int pan4;
  int delay4;
  int depth4;
  int rate4;
  int ducker;             /* 0..3: Off, Dly, Rev or Both */
  int ducker_sensitivity; /* 0..24: SENSITIVITY */
  int ducker_attenuation; /* 0..58: ATTENUATION */
  int ducker_release;     /* 0..19: RELEASE RATE */
  int rev_input;          /* 0..1: Muted or Active */
  int dir_in_pan;         /* 0..100: DIR IN PAN */
  int mix_dir_dly;        /* 0..100: MIX DIR/DLY */
  int reverb_decay;       /* 0..99: REV DECAY */
  int hf_damp;            /* 0..99: RV HF DAMP */
} ifx_chorus_reverb_setup_raw;

int ifx_chorus_reverb_setup_config(const ifx_chorus_reverb_setup_raw *raw,
                                   ifx_chorus_reverb_config *config);

#endif /* IFX_CHORUS_REVERB_SETUP_H */
