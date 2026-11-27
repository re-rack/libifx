#include "ifx_firmware.h"

/* The setup record's words, in ifx_delay_config order: X:$0000 holds the two
 * flags, and the two regen words are the T1 entries REGEN 1 and REGEN 2 select. */
static const ifx_firmware_word k_words[] = {
    IFX_FIRMWARE_X(0x0013), IFX_FIRMWARE_X(0x0014),
    IFX_FIRMWARE_X(0x0015), IFX_FIRMWARE_FLAGS((1u << 4) | (1u << 6)),
    IFX_FIRMWARE_X(0x0006), IFX_FIRMWARE_X(0x0007),
    IFX_FIRMWARE_X(0x0009), IFX_FIRMWARE_Y(0x0018),
    IFX_FIRMWARE_Y(0x0019), IFX_FIRMWARE_Y(0x001A),
    IFX_FIRMWARE_Y(0x001B), IFX_FIRMWARE_Y(0x001E),
    IFX_FIRMWARE_Y(0x001F), IFX_FIRMWARE_Y(0x0022),
    IFX_FIRMWARE_Y(0x0023), IFX_FIRMWARE_REGEN(12),
    IFX_FIRMWARE_REGEN(16), IFX_FIRMWARE_Y(0x0026),
    IFX_FIRMWARE_Y(0x0027), IFX_FIRMWARE_Y(0x0028),
    IFX_FIRMWARE_Y(0x002B),
};

const ifx_firmware ifx_firmware_delay = {
    .effect = &ifx_effect_delay,
    .config = 3,
    .words = k_words,
    .word_count = sizeof k_words / sizeof k_words[0],
};
