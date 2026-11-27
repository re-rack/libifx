/* Intellifex LTD configuration 2: Hush; Reverb.
 *
 * The configuration at P:$00A0 with its eight reverb types: a predelay ring
 * with the LOW FREQ write filter, a per-type 48-word pointer and coefficient
 * table driving the shared reverb lines (four allpass input lines per channel,
 * then four stages of four taps and four damped lines), and the gate on the
 * reverb decay. The front, Hush, the mixer and the memory wipe are shared.
 */
#ifndef IFX_REVERB_H
#define IFX_REVERB_H

#include <stddef.h>
#include <stdint.h>

#include "ifx_front_core.h"
#include "ifx_hush_core.h"
#include "ifx_process.h"
#include "ifx_tables.h"
#include "ifx_wipe_core.h"

/* The words the configuration reads, staged by the message handlers. */
enum {
  IFX_REVERB_COEF_WET = 0,           /* X:$0013: REVERB LVL */
  IFX_REVERB_COEF_DIRECT_LEFT,       /* X:$0014: L DIR LVL */
  IFX_REVERB_COEF_DIRECT_RIGHT,      /* X:$0015: R DIR LVL */
  IFX_REVERB_COEF_FLAGS,             /* X:$0000 bits 4, 6 and 9 */
  IFX_REVERB_COEF_HUSH_THRESHOLD,    /* X:$0006 */
  IFX_REVERB_COEF_HUSH_SLOPE,        /* X:$0007 */
  IFX_REVERB_COEF_HUSH_RELEASE,      /* X:$0009 */
  IFX_REVERB_COEF_DECAY,             /* Y:$0000: REVERB DECAY */
  IFX_REVERB_COEF_DAMP,              /* Y:$0001: RV HF DAMP */
  IFX_REVERB_COEF_FEEDBACK,          /* Y:$0002: DECAY times (1 - DAMP) */
  IFX_REVERB_COEF_LOW_FREQ,          /* Y:$000e: LOW FREQ */
  IFX_REVERB_COEF_TYPE,              /* Y:$000a: REV TYPE */
  IFX_REVERB_COEF_PAN_LEFT,          /* Y:$0008: DIRIN PAN left */
  IFX_REVERB_COEF_PAN_RIGHT,         /* Y:$0009: DIRIN PAN right */
  IFX_REVERB_COEF_PREDELAY_LEFT,     /* Y:$0006: PREDELAY L, samples */
  IFX_REVERB_COEF_PREDELAY_RIGHT,    /* Y:$0007: PREDELAY R, samples */
  IFX_REVERB_COEF_GATE_DECAY,        /* Y:$0003: GATE DECAY */
  IFX_REVERB_COEF_GATE_THRESHOLD,    /* Y:$0004: GATE THRESH */
  IFX_REVERB_COEF_GATE_HOLD,         /* Y:$0005: HOLD TIME, blocks */
  IFX_REVERB_COEF_COUNT
};

enum { IFX_REVERB_FLAG_GATE = 1 << 9 }; /* X:$0000 bit 9: GATE on */

typedef struct {
  int32_t coef[IFX_REVERB_COEF_COUNT];
} ifx_reverb_config;

typedef struct {
  int32_t memory[0x10000]; /* the DSP's one external array, addressed as Y: */
  int32_t wet_left[IFX_BLOCK_FRAMES]; /* Y:$0F40..$0F5F, the configuration's block */
  int32_t wet_right[IFX_BLOCK_FRAMES]; /* Y:$0F60..$0F7F */
  ifx_front_core_state front;
  ifx_hush_core_state hush;
  int32_t table[IFX_REVERB_TYPE_TABLE_WORDS]; /* X:$40..$6F: the type's pairs, advanced in place */
  int32_t type; /* Y:$000a: the live reverb type */
  int32_t gate_gain; /* Y:$000b: the gate's decay word, or GATE DECAY when shut */
  int32_t gate_hold; /* Y:$000c: HOLD TIME countdown, in blocks */
  uint32_t write; /* Y:$000d: predelay ring write pointer */
  int32_t lowpass; /* Y:$002c: the LOW FREQ filter state */
  ifx_wipe_core_state wipe;
  ifx_process_block block;
} ifx_reverb_state;

void ifx_reverb_state_init(ifx_reverb_state *state, const ifx_reverb_config *config);

void ifx_reverb_process_split(ifx_reverb_state *state, const ifx_reverb_config *config,
                              const int32_t *in, int32_t *out_l, int32_t *out_r, size_t frames);

void ifx_reverb_process_f32(ifx_reverb_state *state, const ifx_reverb_config *config,
                            const float *in, float *out_l, float *out_r, size_t frames);

void ifx_reverb_process_interleaved(ifx_reverb_state *state, const ifx_reverb_config *config,
                                    const int32_t *in, int32_t *out, size_t frames);

void ifx_reverb_process_frame(ifx_reverb_state *state, const ifx_reverb_config *config, int32_t in,
                              int32_t out[2]);

#endif /* IFX_REVERB_H */
