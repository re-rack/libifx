#include "ifx_firmware.h"

#include "hatari/dsp_cpu.h"

/* The setup record's words, in ifx_delay_config order: X:$0000 holds the two
 * flags, and the two regen words are the T1 entries REGEN 1 and REGEN 2 select. */
static const ifx_firmware_word k_words[] = {
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0013, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0014, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0015, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0000, .mask = (1u << 4) | (1u << 6)},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0006, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0007, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0009, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x0018, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x0019, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x001A, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x001B, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x001E, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x001F, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x0022, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x0023, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_STAGED, .knob = 12},
    {.kind = IFX_FW_STAGED, .knob = 16},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x0026, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x0027, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x0028, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x002B, .mask = 0xFFFFFFu},
};

const ifx_firmware_ops ifx_firmware_delay = {
    .effect = &ifx_effect_delay,
    .config = 3,
    .words = k_words,
    .word_count = sizeof k_words / sizeof k_words[0],
};
