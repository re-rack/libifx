#include "ifx_firmware.h"

/* The setup record's words, in ifx_reverb_config order: X:$0000 holds the three
 * flags. */
static const ifx_firmware_word k_words[] = {
    IFX_FIRMWARE_X(0x0013), IFX_FIRMWARE_X(0x0014),
    IFX_FIRMWARE_X(0x0015), IFX_FIRMWARE_FLAGS((1u << 4) | (1u << 6) | (1u << 9)),
    IFX_FIRMWARE_X(0x0006), IFX_FIRMWARE_X(0x0007),
    IFX_FIRMWARE_X(0x0009), IFX_FIRMWARE_Y(0x0000),
    IFX_FIRMWARE_Y(0x0001), IFX_FIRMWARE_Y(0x0002),
    IFX_FIRMWARE_Y(0x000E), IFX_FIRMWARE_Y(0x000A),
    IFX_FIRMWARE_Y(0x0008), IFX_FIRMWARE_Y(0x0009),
    IFX_FIRMWARE_Y(0x0006), IFX_FIRMWARE_Y(0x0007),
    IFX_FIRMWARE_Y(0x0003), IFX_FIRMWARE_Y(0x0004),
    IFX_FIRMWARE_Y(0x0005),
};

const ifx_firmware ifx_firmware_reverb = {
    .effect = &ifx_effect_reverb,
    .config = 2,
    .words = k_words,
    .word_count = sizeof k_words / sizeof k_words[0],
};
