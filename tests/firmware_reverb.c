#include "ifx_firmware.h"

#include "hatari/dsp_cpu.h"

/* The setup record's words, in ifx_reverb_config order: X:$0000 holds the three
 * flags. */
static const ifx_firmware_word k_words[] = {
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0013, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0014, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0015, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD,
     .space = DSP_SPACE_X,
     .address = 0x0000,
     .mask = (1u << 4) | (1u << 6) | (1u << 9)},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0006, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0007, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_X, .address = 0x0009, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x0000, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x0001, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x0002, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x000E, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x000A, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x0008, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x0009, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x0006, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x0007, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x0003, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x0004, .mask = 0xFFFFFFu},
    {.kind = IFX_FW_WORD, .space = DSP_SPACE_Y, .address = 0x0005, .mask = 0xFFFFFFu},
};

const ifx_firmware_ops ifx_firmware_reverb = {
    .effect = &ifx_effect_reverb,
    .config = 2,
    .words = k_words,
    .word_count = sizeof k_words / sizeof k_words[0],
};
