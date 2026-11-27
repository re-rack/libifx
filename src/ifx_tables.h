/* The firmware's lookup tables, word for word: the shared DSP image's, one
 * configuration's and the MCU's direct latch curve. */
#ifndef IFX_TABLES_H
#define IFX_TABLES_H

#include <stdint.h>

#define IFX_LEVEL_TABLE_WORDS 59u
#define IFX_PAN_TABLE_WORDS 101u
#define IFX_HUSH_THRESHOLD_TABLE_WORDS 25u
#define IFX_HUSH_SLOPE_TABLE_WORDS 25u
#define IFX_HUSH_RELEASE_TABLE_WORDS 10u
#define IFX_DUCKER_RELEASE_TABLE_WORDS 20u
#define IFX_GATE_DECAY_TABLE_WORDS 32u
#define IFX_GATE_HOLD_TABLE_WORDS 100u
#define IFX_COARSE_PITCH_TABLE_WORDS 181u
#define IFX_FINE_PITCH_TABLE_WORDS 41u
#define IFX_REVERB_TYPE_COUNT 8u
#define IFX_REVERB_TYPE_TABLE_WORDS 48u
#define IFX_DIRECT_TABLE_WORDS 59u

/* P:$07BE..$07F8: T1 level, 0 then -48 dB to 0 dB in 0.5 dB steps. */
extern const int32_t ifx_level_table[IFX_LEVEL_TABLE_WORDS];

/* P:$07F9..$085D: pan cosine, cos(pi k / 200); the right gain reads it backwards. */
extern const int32_t ifx_pan_table[IFX_PAN_TABLE_WORDS];

/* P:$085E..$0876: Hush threshold, 2.5e-5 to 0.1. */
extern const int32_t ifx_hush_threshold_table[IFX_HUSH_THRESHOLD_TABLE_WORDS];

/* P:$0877..$088F: Hush slope matching the threshold table. */
extern const int32_t ifx_hush_slope_table[IFX_HUSH_SLOPE_TABLE_WORDS];

/* P:$0890..$0899: Hush release coefficient. */
extern const int32_t ifx_hush_release_table[IFX_HUSH_RELEASE_TABLE_WORDS];

/* P:$089A..$08AD: Ducker release coefficient. */
extern const int32_t ifx_ducker_release_table[IFX_DUCKER_RELEASE_TABLE_WORDS];

/* P:$08AE..$08CD: gate decay per block. */
extern const int32_t ifx_gate_decay_table[IFX_GATE_DECAY_TABLE_WORDS];

/* P:$08CE..$0931: gate hold in blocks. */
extern const int32_t ifx_gate_hold_table[IFX_GATE_HOLD_TABLE_WORDS];

/* P:$0932..$09E6: 2^(k/60 - 4), pitch in 20-cent steps. */
extern const int32_t ifx_coarse_pitch_table[IFX_COARSE_PITCH_TABLE_WORDS];

/* P:$09E7..$0A0F: 0.5 * 2^((k - 20)/1200), fine pitch in cents. */
extern const int32_t ifx_fine_pitch_table[IFX_FINE_PITCH_TABLE_WORDS];

/* Configuration 2 P:$0172..$02F1: the eight types' 48-word pointer and
 * coefficient tables, copied to X:$40..$6F by the configuration's init. */
extern const int32_t ifx_reverb_type_table[IFX_REVERB_TYPE_COUNT][IFX_REVERB_TYPE_TABLE_WORDS];

/* EPROM 0x31CE: the MCU's direct latch byte per DIR LVL, T1 in 8 bits. */
extern const int32_t ifx_direct_table[IFX_DIRECT_TABLE_WORDS];

#endif /* IFX_TABLES_H */
