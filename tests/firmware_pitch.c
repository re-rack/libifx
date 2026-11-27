#include "ifx_firmware.h"

/* The setup record's words, in ifx_pitch_config order: X:$0000 holds the two
 * flags, the two regen words are the T1 entries REGEN L and REGEN R select, and
 * the four voice blocks run on from Y:$0EC0. */
static const ifx_firmware_word k_words[] = {
    IFX_FIRMWARE_X(0x0013), IFX_FIRMWARE_X(0x0014),
    IFX_FIRMWARE_X(0x0015), IFX_FIRMWARE_FLAGS((1u << 4) | (1u << 6)),
    IFX_FIRMWARE_X(0x0006), IFX_FIRMWARE_X(0x0007),
    IFX_FIRMWARE_X(0x0009), IFX_FIRMWARE_REGEN(4),
    IFX_FIRMWARE_REGEN(5),  IFX_FIRMWARE_Y_RUN(0x0EC0, 32),
};

const ifx_firmware ifx_firmware_pitch = {
    .effect = &ifx_effect_pitch,
    .config = 5,
    .words = k_words,
    .word_count = sizeof k_words / sizeof k_words[0],
};
