#include "ifx_firmware.h"

#include "hatari/dsp_cpu.h"

/* The setup record's words, in ifx_chorus8_config order: X:$0000 holds the two
 * flags, the two regen words are the T1 entries REGEN L and REGEN R select, and
 * the eight voice blocks run on from Y:$0EC0. */
static const ifx_firmware_word k_words[] = {
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0013, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0014, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0015, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0000, .mask = (1u << 4) | (1u << 6)},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0006, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0007, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0009, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_STAGED, .knob = 4},
    {.kind = IFX_FW_STAGED, .knob = 5},
    {.kind = IFX_FW_RANGE, .space = DSP_SPACE_Y, .address = 0x0EC0, .count = 64},
};

const ifx_firmware_ops ifx_firmware_chorus8 = {
    .effect = &ifx_effect_chorus8,
    .config = 4,
    .words = k_words,
    .word_count = sizeof k_words / sizeof k_words[0],
};
