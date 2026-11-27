/* Configuration 4's raw knobs and their conversion to the words its message
 * handlers stage. */
#ifndef IFX_CHORUS8_SETUP_H
#define IFX_CHORUS8_SETUP_H

#include "ifx_base.h"
#include "ifx_chorus8.h"

typedef struct {
  int chorus_level;       /* 0..58: CHORUS LVL */
  int direct_left;        /* 0..58: L DIR LVL */
  int direct_right;       /* 0..58: R DIR LVL */
  int direct_hush;        /* 0..1: PRE or POST */
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
  int level5;
  int pan5;
  int delay5;
  int depth5;
  int rate5;
  int level6;
  int pan6;
  int delay6;
  int depth6;
  int rate6;
  int level7;
  int pan7;
  int delay7;
  int depth7;
  int rate7;
  int level8;
  int pan8;
  int delay8;
  int depth8;
  int rate8;
} ifx_chorus8_setup_raw;

int ifx_chorus8_setup_config(const ifx_chorus8_setup_raw *raw, ifx_chorus8_config *config);

#endif /* IFX_CHORUS8_SETUP_H */
