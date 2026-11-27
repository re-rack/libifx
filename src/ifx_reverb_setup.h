/* Configuration 2's raw knobs and their conversion to the words its message
 * handlers stage. */
#ifndef IFX_REVERB_SETUP_H
#define IFX_REVERB_SETUP_H

#include "ifx_base.h"
#include "ifx_reverb.h"

typedef struct {
  int reverb_level;     /* 0..58: REVERB LVL */
  int direct_left;      /* 0..58: L DIR LVL */
  int direct_right;     /* 0..58: R DIR LVL */
  int direct_hush;      /* 0..1: PRE or POST */
  int hush;             /* 0..1: HUSH I/O */
  int hush_threshold;   /* 0..24: EXP THRESH */
  int hush_release;     /* 0..9: REL RATE */
  int reverb_decay;     /* 0..99: REVERB DECAY */
  int hf_damp;          /* 0..99: RV HF DAMP */
  int low_freq;         /* 0..99: LOW FREQ */
  int reverb_type;      /* 0..7: Plate A/B, Room A/B, Hall A/B, Stadium, Dual */
  int dir_in_pan;       /* 0..100: DIRIN PAN, 0 left to 100 right */
  int predelay_left;    /* 0..209: PREDELAY L, 1 ms steps */
  int predelay_right;   /* 0..209: PREDELAY R, 1 ms steps */
  int gate;             /* 0..1: GATE off or on */
  int gate_decay;       /* 0..31: GATE DECAY */
  int gate_threshold;   /* 0..24: GATE THRESH */
  int gate_hold;        /* 0..99: HOLD TIME */
} ifx_reverb_setup_raw;

int ifx_reverb_setup_config(const ifx_context *context, const ifx_reverb_setup_raw *raw,
                            ifx_reverb_config *config);

#endif /* IFX_REVERB_SETUP_H */
