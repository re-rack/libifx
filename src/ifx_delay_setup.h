/* Configuration 3's raw knobs and their conversion to the words its message
 * handlers stage. */
#ifndef IFX_DELAY_SETUP_H
#define IFX_DELAY_SETUP_H

#include "ifx_base.h"
#include "ifx_delay.h"

typedef struct {
  int delay_level;         /* 0..58: DELAY LVL */
  int direct_left;         /* 0..58: L DIR LVL */
  int direct_right;        /* 0..58: R DIR LVL */
  int direct_hush;         /* 0..1: PRE or POST */
  int analog_direct;       /* 0..1: the analog direct path off or on, heard at DIRECT HUSH PRE */
  int output_level;        /* 0..100: OUTPUT LEVEL, percent; 100 passes the output unchanged */
  int hush;                /* 0..1: HUSH I/O */
  int hush_threshold;      /* 0..24: EXP THRESH */
  int hush_release;        /* 0..9: REL RATE */
  int delay;               /* 0..1: DELAY muted or active */
  int mute_type;           /* 0..2: PRE, POST or BOTH */
  int level1;              /* 0..58: LEVEL 1 */
  int pan1;                /* 0..100: PAN 1 */
  int time1;               /* 0..150: DLY TIME 1, 5 ms steps */
  int regen1;              /* 0..58: REGEN 1 */
  int level2;              /* 0..58: LEVEL 2 */
  int pan2;                /* 0..100: PAN 2 */
  int time2;               /* 0..150: DLY TIME 2, 5 ms steps */
  int regen2;              /* 0..58: REGEN 2 */
  int delay_type;          /* 0..2: Stereo, Ping-Pong or 2-Tap */
  int hf_damp;             /* 0..99: DL HF DAMP */
  int ducker;              /* 0..1: DUCKER off or on */
  int ducker_sensitivity;  /* 0..24: SENSITIVITY */
  int ducker_attenuation;  /* 0..58: ATTENUATION */
  int ducker_release;      /* 0..19: RELEASE RATE */
} ifx_delay_setup_raw;

int ifx_delay_setup_config(const ifx_delay_setup_raw *raw, ifx_delay_config *config);

#endif /* IFX_DELAY_SETUP_H */
