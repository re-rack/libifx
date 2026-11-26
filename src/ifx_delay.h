/* Intellifex LTD configuration 3: Hush; Delay; Ducker.
 *
 * The configuration at P:$00A0 with its three delay types: Stereo (two mono
 * lines), Ping-Pong (cross-fed mono lines) and 2-Tap (one interleaved line at
 * half the pointer rate). The ducker and the mixer are the shared routines.
 */
#ifndef IFX_DELAY_H
#define IFX_DELAY_H

#include <stddef.h>
#include <stdint.h>

#include "ifx_ducker_core.h"
#include "ifx_front_core.h"
#include "ifx_hush_core.h"
#include "ifx_process.h"
#include "ifx_wipe_core.h"

#define IFX_DELAY_LINE_WORDS 0x73C0u

/* The words the configuration reads, staged by the message handlers, in the
 * configuration's parameter order. */
enum {
  IFX_DELAY_COEF_WET = 0,        /* X:$0013: DELAY LVL */
  IFX_DELAY_COEF_DIRECT_LEFT,    /* X:$0014: L DIR LVL */
  IFX_DELAY_COEF_DIRECT_RIGHT,   /* X:$0015: R DIR LVL */
  IFX_DELAY_COEF_FLAGS,          /* X:$0000 bits 4 and 6 */
  IFX_DELAY_COEF_HUSH_THRESHOLD, /* X:$0006 */
  IFX_DELAY_COEF_HUSH_SLOPE,     /* X:$0007 */
  IFX_DELAY_COEF_HUSH_RELEASE,   /* X:$0009 */
  IFX_DELAY_COEF_DELAY_FLAGS,    /* Y:$0018: the delay, mute and type bits */
  IFX_DELAY_COEF_DAMP,           /* Y:$0019: DL HF DAMP */
  IFX_DELAY_COEF_TIME1,          /* Y:$001A: DLY TIME 1 */
  IFX_DELAY_COEF_TIME2,          /* Y:$001B: DLY TIME 2 */
  IFX_DELAY_COEF_GAIN1_LEFT,     /* Y:$001E: LEVEL 1 and PAN 1, left */
  IFX_DELAY_COEF_GAIN1_RIGHT,    /* Y:$001F: LEVEL 1 and PAN 1, right */
  IFX_DELAY_COEF_GAIN2_LEFT,     /* Y:$0022: LEVEL 2 and PAN 2, left */
  IFX_DELAY_COEF_GAIN2_RIGHT,    /* Y:$0023: LEVEL 2 and PAN 2, right */
  IFX_DELAY_COEF_REG_LEFT,       /* Y:$0024: REGEN 1 */
  IFX_DELAY_COEF_REG_RIGHT,      /* Y:$0025: REGEN 2 */
  IFX_DELAY_COEF_DUCK_THRESHOLD, /* Y:$0026: SENSITIVITY */
  IFX_DELAY_COEF_DUCK_ATTEN,     /* Y:$0027: ATTENUATION */
  IFX_DELAY_COEF_DUCK_RELEASE,   /* Y:$0028: RELEASE RATE */
  IFX_DELAY_COEF_DUCKER,         /* Y:$002B: DUCKER on/off */
  IFX_DELAY_COEF_COUNT
};

/* Delay flag bits in Y:$0018. */
enum {
  IFX_DELAY_ON = 1 << 0,
  IFX_DELAY_MUTE_PRE = 1 << 1,
  IFX_DELAY_MUTE_POST = 1 << 2,
  IFX_DELAY_TYPE_STEREO = 1 << 3,
  IFX_DELAY_TYPE_PINGPONG = 1 << 4,
  IFX_DELAY_TYPE_2TAP = 1 << 5,
};

typedef struct {
  int32_t coef[IFX_DELAY_COEF_COUNT];
} ifx_delay_config;

typedef struct {
  int32_t memory[0x10000]; /* the DSP's one external array, addressed as Y: */
  int32_t wet_left[IFX_BLOCK_FRAMES]; /* Y:$0F40..$0F5F, the configuration's block */
  int32_t wet_right[IFX_BLOCK_FRAMES]; /* Y:$0F60..$0F7F */
  ifx_front_core_state front;
  ifx_hush_core_state hush;
  ifx_ducker_core_state ducker;
  uint32_t write1; /* Y:$0015: line 1 pointer, or the 2-Tap pair pointer */
  uint32_t write2; /* Y:$0016: line 2 pointer */
  int32_t damp1;   /* Y:$002C: line 1 damping state */
  int32_t damp2;   /* Y:$002D: line 2 damping state */
  int32_t regen1;  /* the live Y:$0024 word */
  int32_t regen2;  /* the live Y:$0025 word */
  int32_t staged_regen1;
  int32_t staged_regen2;
  int32_t delay_flags; /* Y:$0018: the live delay, mute and type bits */
  ifx_wipe_core wipe;
  ifx_process_block block;
} ifx_delay_state;

void ifx_delay_state_init(ifx_delay_state *state, const ifx_delay_config *config);

void ifx_delay_process_split(ifx_delay_state *state, const ifx_delay_config *config,
                             const int32_t *in, int32_t *out_l, int32_t *out_r, size_t frames);

void ifx_delay_process_f32(ifx_delay_state *state, const ifx_delay_config *config, const float *in,
                           float *out_l, float *out_r, size_t frames);

void ifx_delay_process_interleaved(ifx_delay_state *state, const ifx_delay_config *config,
                                   const int32_t *in, int32_t *out, size_t frames);

void ifx_delay_process_frame(ifx_delay_state *state, const ifx_delay_config *config, int32_t in,
                             int32_t out[2]);

#endif /* IFX_DELAY_H */
