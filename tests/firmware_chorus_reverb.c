#include "ifx_firmware.h"

/* The setup record's words, in ifx_chorus_reverb_config order: X:$0000 holds the
 * three flags, the two regen words are the T1 entries REGEN L and REGEN R
 * select, and the four voice blocks run on from Y:$0EC0. */
static const ifx_firmware_word k_words[] = {
    IFX_FIRMWARE_X(0x0013),         IFX_FIRMWARE_X(0x0014),
    IFX_FIRMWARE_X(0x0015),         IFX_FIRMWARE_FLAGS((1u << 4) | (1u << 6) | (1u << 17)),
    IFX_FIRMWARE_X(0x0020),         IFX_FIRMWARE_X(0x0021),
    IFX_FIRMWARE_X(0x0022),         IFX_FIRMWARE_REGEN(7),
    IFX_FIRMWARE_REGEN(8),          IFX_FIRMWARE_X(0x0006),
    IFX_FIRMWARE_X(0x0007),         IFX_FIRMWARE_X(0x0009),
    IFX_FIRMWARE_Y_RUN(0x0EC0, 32), IFX_FIRMWARE_Y(0x002B),
    IFX_FIRMWARE_Y(0x0026),         IFX_FIRMWARE_Y(0x0027),
    IFX_FIRMWARE_Y(0x0028),         IFX_FIRMWARE_Y(0x0008),
    IFX_FIRMWARE_Y(0x0009),         IFX_FIRMWARE_X(0x0025),
    IFX_FIRMWARE_X(0x0026),         IFX_FIRMWARE_Y(0x0002),
    IFX_FIRMWARE_Y(0x0001),
};

const ifx_firmware ifx_firmware_chorus_reverb = {
    .effect = &ifx_effect_chorus_reverb,
    .config = 0,
    .words = k_words,
    .word_count = sizeof k_words / sizeof k_words[0],
};
