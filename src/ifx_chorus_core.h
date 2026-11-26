/* The chorus network configuration 4 runs: the regeneration limiter and sum,
 * the 16-tap and 6-tap smoothing filters, the eight interpolated LFO voices,
 * and the rotating L/R delay lines. Configuration 0 runs the same voice
 * network with four voices.
 */
#ifndef IFX_CHORUS_CORE_H
#define IFX_CHORUS_CORE_H

#include <stdint.h>

#define IFX_CHORUS_VOICES 8u
#define IFX_CHORUS_LINE_WORDS 0x4000u

typedef struct {
  int32_t memory[0x10000]; /* the DSP's one external array, addressed as Y: */
  int32_t history[64];     /* X:$40..$7F: the input and regeneration sum */
  int32_t sweep[64];       /* Y:$40..$7F: pointer pairs the voices read */
  int32_t phase[IFX_CHORUS_VOICES][4]; /* Y:$1820..$183F: sweep and LFO phases */
  uint32_t history_write; /* Y:$11 */
  uint32_t filter_write;  /* Y:$12 */
  uint32_t filter_read;   /* Y:$13 */
  uint32_t filter2_write; /* Y:$14 */
  uint32_t read_l;        /* Y:$15 */
  uint32_t read_r;        /* Y:$16 */
  int32_t regen_l;        /* Y:$000F, the live word the limiter scales */
  int32_t regen_r;        /* Y:$0010 */
  int32_t staged_regen_l;
  int32_t staged_regen_r;
} ifx_chorus_core_state;

void ifx_chorus_core_state_init(ifx_chorus_core_state *state, int32_t regen_l, int32_t regen_r);

/* One block: `voices` is IFX_CHORUS_VOICES blocks of eight words at $0EC0 +
 * 8*v; the regen words are the handler's staged values. */
void ifx_chorus_core_process(ifx_chorus_core_state *state, const int32_t *voices, int32_t regen_l,
                             int32_t regen_r, const int32_t input[32], int32_t out_l[32],
                             int32_t out_r[32]);

#endif /* IFX_CHORUS_CORE_H */
