/* The pitch network configurations 5 and 1 run: the regeneration limiter over
 * the voice pan gains, the input sum, the 16-tap filter, configuration 5's
 * 6-tap filter, the two-grain voices and the L/R delay lines. Hush, the front
 * and the mixer are shared.
 */
#ifndef IFX_PITCH_CORE_H
#define IFX_PITCH_CORE_H

#include <stdint.h>

#define IFX_PITCH_MAX_VOICES 4u

/* The addresses and ring sizes the pitch kernel works in. Configuration 5 uses
 * both filters; configuration 1 runs the 16-tap filter straight into the
 * larger ring. */
typedef struct {
  unsigned voices;
  unsigned phase_bits; /* phase word width: 12 for configuration 1, 13 for 5 */
  unsigned rate_shift;
  unsigned filter1_base; /* Y:$1C00 / configuration 1 unused */
  unsigned filter1_m;    /* $7F */
  unsigned filter2_base; /* Y:$2000 */
  unsigned filter2_m;    /* $1FFF / $0FFF */
  unsigned dual_filter;  /* configuration 5 runs the 6-tap filter */
  unsigned grain_phase;  /* the phase table's initial integer word */
  unsigned line_l;
  unsigned line_r;
  unsigned line_m;
} ifx_pitch_core_layout;

typedef struct {
  int32_t memory[0x10000]; /* the DSP's one external array, addressed as Y: */
  int32_t history[64];     /* X:$40..$7F */
  int32_t window[256];     /* Y:$1800..$18FF or Y:$1000..$10FF */
  int32_t grain[2][32];    /* Y:$1C80/$1CA0 or Y:$1180/$11A0 */
  int32_t voice[IFX_PITCH_MAX_VOICES][7]; /* Y:$1CC0: two phase pairs, rings, window */
  uint32_t history_write; /* Y:$0011 */
  uint32_t filter1_write; /* Y:$0013 */
  uint32_t filter2_write; /* Y:$0012 */
  uint32_t read_l;        /* Y:$0015 */
  uint32_t read_r;        /* Y:$0016 */
  int32_t regen_l;        /* Y:$000F, the live word the limiter scales */
  int32_t regen_r;        /* Y:$0010 */
  int32_t staged_regen_l;
  int32_t staged_regen_r;
} ifx_pitch_core_state;

void ifx_pitch_core_state_init(ifx_pitch_core_state *state, const ifx_pitch_core_layout *layout,
                               int32_t regen_l, int32_t regen_r);

/* One block: `voices` is layout->voices blocks of eight words at $0EC0 + 8*v;
 * the regen words are the handlers' staged values. */
void ifx_pitch_core_process(ifx_pitch_core_state *state, const ifx_pitch_core_layout *layout,
                            const int32_t *voices, int32_t regen_l, int32_t regen_r,
                            const int32_t input[32], int32_t out_l[32], int32_t out_r[32]);

#endif /* IFX_PITCH_CORE_H */
