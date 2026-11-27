#include "ifx_firmware.h"

#include "hatari/dsp_cpu.h"

/* The setup record's words, in ifx_pitch_reverb_config order: X:$0000 holds the
 * three flags, the two regen words are the T1 entries REGEN L and REGEN R
 * select, and the two pitch voices and Delay 3 run on from Y:$0EC0. */
static const ifx_firmware_word k_words[] = {
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0013, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0014, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0015, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD,
     .space = DSP_SPACE_X,
     .address = 0x0000,
     .mask = (1u << 4) | (1u << 6) | (1u << 17)},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0020, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0021, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0022, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_STAGED, .knob = 7},
    {.kind = IFX_FW_STAGED, .knob = 8},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0006, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0007, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0009, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_RANGE, .space = DSP_SPACE_Y, .address = 0x0EC0, .count = 24},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x002B, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x0026, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x0027, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x0028, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x0008, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x0009, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0025, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0026, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x0002, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x0001, .mask = 0xFFFFFFu},
};

const ifx_firmware_ops ifx_firmware_pitch_reverb = {
    .effect = &ifx_effect_pitch_reverb,
    .config = 1,
    .words = k_words,
    .word_count = sizeof k_words / sizeof k_words[0],
};
