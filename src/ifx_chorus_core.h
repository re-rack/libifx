/* The chorus network configurations 0 and 4 run: the regeneration frame, the
 * 16-tap smoothing filter into the voice history (configuration 4 adds a
 * 6-tap filter), and the LFO voices, each a sweep of interpolated reads added
 * into the L/R delay lines at its delay. Configuration 0 runs four voices and
 * sums its zero-delay voices into the chorus bus; configuration 4 runs eight.
 * The routine addresses are configuration 4's.
 */
#ifndef IFX_CHORUS_CORE_H
#define IFX_CHORUS_CORE_H

#include <stdint.h>

#include "ifx_base.h"
#include "ifx_regen_core.h"

#define IFX_CHORUS_CORE_MAX_VOICES 8u

/* Where the two configurations' copies of the network differ. The kernels
 * step both rings with a mask, so each M is 2^k - 1, and the sweep window
 * divides $2000. */
typedef struct {
  unsigned voices;
  int32_t phase_step;     /* each voice's initial sweep phase */
  uint32_t filter_write;  /* Y:$0012 at init: the 16-tap output pointer */
  uint32_t filter_read;   /* Y:$0013 at init: the 6-tap input pointer */
  uint32_t filter2_write; /* Y:$0014 at init: the 6-tap output pointer */
  uint32_t filter_m;      /* the 16-tap output ring */
  int32_t filter_stride;  /* words per 16-tap output: 1, or 2 when the 6-tap filter interleaves */
  int six_tap;            /* configuration 4's 6-tap filter */
  int voice_bus;          /* zero-delay voices go to the chorus bus */
  int32_t lfo_base;       /* the LFO table, Y:$1000 or Y:$1800 */
  int clamp_filter2;      /* the phase clamp reads Y:$0014, not Y:$0012 */
  int32_t clamp_window;   /* the clamp's reach around the filter pointer */
  uint32_t sweep_m;       /* the voices' read ring */
  int extra_asr;          /* the depth product takes an extra ASR */
} ifx_chorus_core_layout;

typedef struct {
  ifx_regen_core_state regen;                             /* Y:$000F/$0010 */
  int32_t history[IFX_REGEN_CORE_HISTORY_WORDS];          /* X:$40..$7F */
  int32_t voice_bus[2u * IFX_BLOCK_FRAMES];               /* Y:$1040..$107F */
  int32_t phase[IFX_CHORUS_CORE_MAX_VOICES][4]; /* Y:$1820..$183F: sweep and LFO phases */
  uint32_t history_write; /* Y:$0011 */
  uint32_t filter_write; /* Y:$0012 */
  uint32_t filter_read; /* Y:$0013 */
  uint32_t filter2_write; /* Y:$0014 */
  uint32_t read_l; /* Y:$0015 */
  uint32_t read_r; /* Y:$0016 */
} ifx_chorus_core_state;

void ifx_chorus_core_state_init(ifx_chorus_core_state *state, const ifx_chorus_core_layout *layout,
                                int32_t regen_l, int32_t regen_r);

/* One block: `voices` is layout->voices blocks of eight words at $0EC0 + 8*v;
 * the regen words are the handlers' staged values. The lines' oldest block is
 * left for the caller to read. */
void ifx_chorus_core_process(ifx_chorus_core_state *state, const ifx_chorus_core_layout *layout,
                             int32_t memory[0x10000], const int32_t *voices, int32_t regen_l,
                             int32_t regen_r, const int32_t input[IFX_BLOCK_FRAMES]);

#endif /* IFX_CHORUS_CORE_H */
