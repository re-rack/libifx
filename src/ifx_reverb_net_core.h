/* The reverb networks. Both reverb programs build on the same three lines over
 * a 32-word block: the two-word allpass, the tap sum and the damped allpass,
 * each walking pointer/coefficient pairs that advance in place. Configuration
 * 2 runs them over its per-type table; configurations 0 and 1 share the
 * network at $C000 (14 pairs: two allpass input lines, then four stages of
 * three taps and three damped lines alternating left and right) and the
 * output mix after it.
 */
#ifndef IFX_REVERB_NET_CORE_H
#define IFX_REVERB_NET_CORE_H

#include <stdint.h>

#include "ifx_base.h"

#define IFX_REVERB_NET_CORE_TABLE_WORDS 28u

/* `lines` two-word allpass lines, each a pointer/coefficient pair of `table`,
 * all fed by `block`, in the modulo-`m` memory window. */
void ifx_reverb_net_core_allpass(int32_t *table, int32_t memory[0x10000],
                                 const int32_t block[IFX_BLOCK_FRAMES], unsigned lines, uint32_t m);

/* Two to four delay taps at the pointers table[0], table[2], ..., each times
 * `gain`, summed into `block`. */
void ifx_reverb_net_core_taps(const int32_t *table, const int32_t memory[0x10000], unsigned taps,
                              int32_t gain, uint32_t m, int32_t block[IFX_BLOCK_FRAMES]);

/* `lines` damped allpass lines over `block`, each a read/write pointer pair. */
void ifx_reverb_net_core_damped(int32_t *table, int32_t memory[0x10000],
                                const int32_t block[IFX_BLOCK_FRAMES], unsigned lines, int32_t damp,
                                int32_t gain, uint32_t m);

typedef struct {
  int32_t table[IFX_REVERB_NET_CORE_TABLE_WORDS]; /* X:$1080..$109B, advanced in place */
} ifx_reverb_net_core_state;

void ifx_reverb_net_core_state_init(ifx_reverb_net_core_state *state);

/* Configuration 0's P:$00DD onwards: while REV INPUT is active, the Hush block
 * panned by DIR IN PAN and scaled by MIX DIR/DLY's direct word, plus the delay
 * returns at `delay_l`/`delay_r` scaled by its delay word, ducked when DUCKER
 * bit 0 is set, through the network. */
void ifx_reverb_net_core_process(ifx_reverb_net_core_state *state, int32_t memory[0x10000],
                                 const int32_t input[IFX_BLOCK_FRAMES], uint32_t delay_l,
                                 uint32_t delay_r, int32_t pan_l, int32_t pan_r, int32_t mix_l,
                                 int32_t mix_r, int32_t ducker_gain, int32_t ducker_mode,
                                 int rev_input, int32_t damp, int32_t feedback,
                                 int32_t out_l[IFX_BLOCK_FRAMES], int32_t out_r[IFX_BLOCK_FRAMES]);

/* Configuration 0's P:$0119: the zero-delay voice bus, the delay lines'
 * oldest block and the reverb into the wet block at their levels, DUCKER bit 0
 * ducking the delay and bit 1 the reverb; the bus and the line words read are
 * cleared. */
void ifx_reverb_net_core_mix(int32_t memory[0x10000], int32_t bus[2u * IFX_BLOCK_FRAMES],
                             uint32_t *read_l, uint32_t *read_r, int32_t bus_level,
                             int32_t delay_level, int32_t reverb_level, int32_t ducker_gain,
                             int32_t ducker_mode, const int32_t reverb_l[IFX_BLOCK_FRAMES],
                             const int32_t reverb_r[IFX_BLOCK_FRAMES],
                             int32_t wet_l[IFX_BLOCK_FRAMES], int32_t wet_r[IFX_BLOCK_FRAMES]);

#endif /* IFX_REVERB_NET_CORE_H */
