/* The regenerating delay lines. The live Regen words and the limiter that
 * scales them serve configurations 0, 1, 3, 4 and 5; the L/R line frame the
 * voice configurations 0, 1, 4 and 5 share is the block's input plus the
 * regenerated returns into the 64-word history, the voices added into the
 * lines at their delays, and the read-out of the lines' oldest block.
 */
#ifndef IFX_REGEN_CORE_H
#define IFX_REGEN_CORE_H

#include <stdint.h>

#include "ifx_base.h"

/* The lines are 16,384-word rings. */
#define IFX_REGEN_CORE_LINE_M 0x3FFFu
#define IFX_REGEN_CORE_HISTORY_WORDS 64u

typedef struct {
  int32_t left;         /* the live REGEN L / REGEN 1 word the limiter scales */
  int32_t right;        /* the live REGEN R / REGEN 2 word */
  int32_t staged_left;  /* the word the handler last staged */
  int32_t staged_right;
} ifx_regen_core_state;

void ifx_regen_core_state_init(ifx_regen_core_state *state, int32_t left, int32_t right);

/* A Regen turn rewrites only its own live word, so the other stays scaled. */
void ifx_regen_core_stage(ifx_regen_core_state *state, int32_t left, int32_t right);

/* While left + right reaches 1.0, both live words are scaled by 0.8889:
 * configuration 3's 2-Tap limiter. */
void ifx_regen_core_limit(ifx_regen_core_state *state);

/* The same scaling while the voices' pan gains (words +6 and +7 of each
 * eight-word voice block) times the live words sum to 1.0 or more. */
void ifx_regen_core_limit_voices(ifx_regen_core_state *state, const int32_t *voices,
                                 unsigned count);

/* The block's input plus both lines' words at `read_l`/`read_r` times the live
 * words into the history at `history_write`, which stays put. */
void ifx_regen_core_feed(const ifx_regen_core_state *state, const int32_t memory[0x10000],
                         uint32_t read_l, uint32_t read_r, const int32_t input[IFX_BLOCK_FRAMES],
                         int32_t history[IFX_REGEN_CORE_HISTORY_WORDS], uint32_t history_write);

/* One voice's block into both lines at `delay` words past the read pointers,
 * times its pan gains `gain_l`/`gain_r`. With a `bus`, a zero-delay voice goes
 * into the bus's left and right halves instead. */
void ifx_regen_core_add(int32_t memory[0x10000], int32_t *bus, uint32_t read_l, uint32_t read_r,
                        int32_t delay, int32_t gain_l, int32_t gain_r,
                        const int32_t voice[IFX_BLOCK_FRAMES]);

/* The oldest block of both lines into the output, clearing the words read. */
void ifx_regen_core_drain(int32_t memory[0x10000], uint32_t *read_l, uint32_t *read_r,
                          int32_t out_l[IFX_BLOCK_FRAMES], int32_t out_r[IFX_BLOCK_FRAMES]);

#endif /* IFX_REGEN_CORE_H */
