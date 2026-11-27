/* The pitch network configurations 1 and 5 run: the regeneration frame, the
 * 16-tap filter into the grain history (configuration 5 adds a 6-tap filter),
 * and the voices, each two swept grains crossfaded by the window and added
 * into the L/R delay lines at its delay. Configuration 5 runs four voices;
 * configuration 1 runs two, sums its zero-delay voices into the bus, and adds
 * its Delay 3 voice from the history. The routine addresses are configuration
 * 5's.
 */
#ifndef IFX_PITCH_CORE_H
#define IFX_PITCH_CORE_H

#include <stdint.h>

#include "ifx_base.h"
#include "ifx_regen_core.h"

#define IFX_PITCH_CORE_MAX_VOICES 4u

/* Where the two configurations' copies of the network differ. The kernels
 * index both rings from their base with a mask, so each M is 2^k - 1, each
 * base a multiple of M + 1, and filter2_m the grain phase's mask. */
typedef struct {
  unsigned voices;
  unsigned phase_bits;   /* the grain phase's integer width: 12 or 13 */
  unsigned rate_shift;   /* the rate word's left shift into the phase step */
  uint32_t filter1_base; /* Y:$1C00: configuration 5's 128-word 16-tap ring */
  uint32_t filter1_m;
  uint32_t filter2_base; /* Y:$2000: the ring the grains read */
  uint32_t filter2_m;
  int six_tap;           /* configuration 5's 6-tap filter between the rings */
  int32_t grain_phase;   /* each grain's initial integer phase */
  uint32_t window_base;  /* the crossfade window, Y:$1800 or Y:$1000 */
  uint32_t line_l;       /* Y:$0015 at init */
  uint32_t line_r;       /* Y:$0016 at init */
  int delay_voice;       /* configuration 1's Delay 3 voice after the pitch voices */
  int voice_bus;         /* zero-delay voices go to the voice bus */
} ifx_pitch_core_layout;

typedef struct {
  ifx_regen_core_state regen;                    /* Y:$000F/$0010 */
  int32_t history[IFX_REGEN_CORE_HISTORY_WORDS]; /* X:$40..$7F */
  int32_t voice[IFX_PITCH_CORE_MAX_VOICES][7];   /* Y:$1CC0 or Y:$11C0: grain phases, window */
  int32_t voice_bus[2u * IFX_BLOCK_FRAMES];      /* Y:$11E0..$121F */
  uint32_t history_write;                        /* Y:$0011 */
  uint32_t filter1_write;                        /* Y:$0013 */
  uint32_t filter2_write;                        /* Y:$0012 */
  uint32_t read_l;                               /* Y:$0015 */
  uint32_t read_r;                               /* Y:$0016 */
} ifx_pitch_core_state;

/* Also builds the crossfade window in `memory`. */
void ifx_pitch_core_state_init(ifx_pitch_core_state *state, const ifx_pitch_core_layout *layout,
                               int32_t memory[0x10000], int32_t regen_l, int32_t regen_r);

/* One block: `voices` is the pitch voices plus, for configuration 1, the Delay
 * 3 block, each eight words at $0EC0 + 8*v; the regen words are the handlers'
 * staged values. The lines' oldest block is left for the caller to read. */
void ifx_pitch_core_process(ifx_pitch_core_state *state, const ifx_pitch_core_layout *layout,
                            int32_t memory[0x10000], const int32_t *voices, int32_t regen_l,
                            int32_t regen_r, const int32_t input[IFX_BLOCK_FRAMES]);

#endif /* IFX_PITCH_CORE_H */
