/* The chorus network configurations 0 and 4 run: the regeneration limiter and
 * sum, the 16-tap and 6-tap smoothing filters, the interpolated LFO voices,
 * and the rotating L/R delay lines. Configuration 4 runs eight voices through
 * the 6-tap filter; configuration 0 four voices through a 4,096-word filter
 * history, its zero-delay voices summed into the chorus bus.
 */
#ifndef IFX_CHORUS_CORE_H
#define IFX_CHORUS_CORE_H

#include <stdint.h>

#define IFX_CHORUS_MAX_VOICES 8u
#define IFX_CHORUS_LINE_WORDS 0x4000u
#define IFX_CHORUS_HISTORY_WORDS 0x40u

typedef struct {
  unsigned voices;
  unsigned phase_step;
  unsigned filter_write; /* Y:$0012, the 16-tap output pointer */
  unsigned filter_read;  /* Y:$0013, the 6-tap input pointer */
  unsigned filter2_write; /* Y:$0014, the 6-tap output pointer */
  unsigned filter_m;      /* the 16-tap output ring */
  unsigned filter_stride; /* words per output, 1 or 2 */
  int has_six_tap;
  int voice_bus;      /* zero-delay voices go to Y:$1040/$1060 */
  unsigned lfo_base;  /* Y:$1000 or Y:$1800 */
  int clamp_filter2;  /* the phase clamp reads Y:$0014, not Y:$0012 */
  unsigned clamp_window;
  unsigned sweep_m;
  int extra_asr;      /* the LFO depth word takes an extra ASR */
  int final_line_read;
} ifx_chorus_core_geometry;

extern const ifx_chorus_core_geometry ifx_chorus_core_geometry_cfg0;
extern const ifx_chorus_core_geometry ifx_chorus_core_geometry_cfg4;

typedef struct {
  const ifx_chorus_core_geometry *geometry;
  int32_t history[IFX_CHORUS_HISTORY_WORDS]; /* X:$40..$7F: the input and regeneration sum */
  int32_t sweep[64];                         /* Y:$40..$7F: pointer pairs the voices read */
  int32_t voice_out[32];                     /* X:$C0..$DF: configuration 0's interpolated taps */
  int32_t voice_bus[64]; /* Y:$1040..$107F: configuration 0's zero-delay sum */
  int32_t phase[IFX_CHORUS_MAX_VOICES][4]; /* Y:$1820..$183F: sweep and LFO phases */
  uint32_t history_write; /* Y:$11 */
  uint32_t filter_write; /* Y:$12 */
  uint32_t filter_read; /* Y:$13 */
  uint32_t filter2_write; /* Y:$14 */
  uint32_t read_l; /* Y:$15 */
  uint32_t read_r; /* Y:$16 */
  int32_t regen_l; /* Y:$000F, the live word the limiter scales */
  int32_t regen_r; /* Y:$0010 */
  int32_t staged_regen_l;
  int32_t staged_regen_r;
} ifx_chorus_core_state;

void ifx_chorus_core_state_init(ifx_chorus_core_state *state,
                                const ifx_chorus_core_geometry *geometry, int32_t regen_l,
                                int32_t regen_r);

/* One block: `voices` is geometry->voices blocks of eight words at $0EC0 +
 * 8*v; the regen words are the handler's staged values. */
void ifx_chorus_core_process(ifx_chorus_core_state *state, int32_t memory[0x10000],
                             const int32_t *voices, int32_t regen_l, int32_t regen_r,
                             const int32_t input[32], int32_t out_l[32], int32_t out_r[32]);

#endif /* IFX_CHORUS_CORE_H */
