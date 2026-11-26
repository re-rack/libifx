/* Configuration 5's raw knobs and their conversion to the words its message
 * handlers stage. */
#ifndef IFX_PITCH_SETUP_H
#define IFX_PITCH_SETUP_H

#include "ifx_base.h"
#include "ifx_pitch.h"

typedef struct {
  int pitch_level;        /* 0..58: PSHIFT LVL */
  int direct_left;        /* 0..58: L DIR LVL */
  int direct_right;       /* 0..58: R DIR LVL */
  int direct_hush;        /* 0..1: PRE or POST */
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
  int pitch3;
  int fine3;
  int level3;
  int pan3;
  int delay3;
  int pitch4;
  int fine4;
  int level4;
  int pan4;
  int delay4;
} ifx_pitch_setup_raw;

int ifx_pitch_setup_config(const ifx_context *context, const ifx_pitch_setup_raw *raw,
                           ifx_pitch_config *config);

#endif /* IFX_PITCH_SETUP_H */
