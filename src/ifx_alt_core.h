/* The crossfade the de-popped variants (ifx_X_alt) share. A variant runs its
 * effect at all times. A turn would step the effect's output, so while the
 * effect is heard, a turn first freezes a copy of it, which keeps the old
 * settings and plays on; the effect takes the turn unheard. Once the effect's
 * output has passed every step the turn makes in it, `hold` blocks on, the
 * output fades from the copy to the effect along the eased ramp (ifx_fade.h),
 * and from there it is the effect's, sample for sample.
 *
 * A turn during the fade freezes another copy, which then fades in instead of
 * the effect, and the effect follows once it is clean again; so at most two
 * copies run, one fading out and one fading in or waiting. A turn while only
 * copies are heard just extends the wait.
 */
#ifndef IFX_ALT_CORE_H
#define IFX_ALT_CORE_H

#include <stddef.h>
#include <stdint.h>

/* The sources: the effect, then the two copies. */
enum { IFX_ALT_CORE_EXACT = 0, IFX_ALT_CORE_SOURCES = 3, IFX_ALT_CORE_NONE = IFX_ALT_CORE_SOURCES };

typedef struct {
  unsigned from; /* the source fading out, IFX_ALT_CORE_NONE while one plays alone */
  unsigned to;   /* the source fading in, or playing alone */
  uint32_t pos;  /* frames into the fade */
  uint32_t wait; /* blocks until the effect has passed its last turn's steps */
} ifx_alt_core_state;

/* The effect alone. */
void ifx_alt_core_state_init(ifx_alt_core_state *state);

/* A turn the effect takes with the block about to complete, whose steps reach
 * its output within `hold` more blocks. Returns the copy to freeze the effect
 * into before it takes the turn, or IFX_ALT_CORE_NONE. */
unsigned ifx_alt_core_turn(ifx_alt_core_state *state, uint32_t hold);

/* Whether `source` runs: the effect always, a copy while it is heard. */
int ifx_alt_core_runs(const ifx_alt_core_state *state, unsigned source);

/* `frames` output frames from the running sources' frames; the others are
 * NULL. */
void ifx_alt_core_mix(ifx_alt_core_state *state, const int32_t *const left[IFX_ALT_CORE_SOURCES],
                      const int32_t *const right[IFX_ALT_CORE_SOURCES], int32_t *out_left,
                      int32_t *out_right, size_t frames);

/* The end of a block: the fade to the effect starts once it is clean. */
void ifx_alt_core_block(ifx_alt_core_state *state);

/* The hold for a step `samples` behind the turn, with a block to spare. */
uint32_t ifx_alt_core_blocks(int32_t samples);

/* The hold for a turn of `voices` voice blocks of eight words: a voice adds
 * into the lines at its delay, word 3 in samples, so a turn of its delay or
 * its gains (words 3 to 7) reaches the output that much later. Its rate,
 * depth and pitch words move its sweep without a step. */
uint32_t ifx_alt_core_voices(const int32_t *from, const int32_t *to, unsigned voices);

#endif /* IFX_ALT_CORE_H */
