# Vendored DSP56001 core

Hatari's Falcon DSP emulation, taken from upstream so the firmware runs against
a real DSP56001 instruction set instead of a model of our own.

- Upstream: https://github.com/hatari/hatari.git, commit
  `36a8acf02bb3f08749d1a4db165f85328e60793b`, directory `src/falcon/`.
- Files kept: `dsp_core.c`, `dsp_core.h`, `dsp_cpu.c`, `dsp_cpu.h`,
  `dsp_disasm.c`, `dsp_disasm.h`, `COPYING`.
- Not vendored: `dsp.c`, `dsp.h`, `crossbar.c`, `crossbar.h` and the rest of
  Hatari. The board glue in `../ifx_dsp_board.c` drives the core directly.
- Licence: GPL-2.0-or-later, see `COPYING`.

Instruction decoding, cycle counting and the register set are upstream's.
The differences below are the whole diff; `make dsp-core-check` pins the
memory map down, and the checks in `../ifx_mcu_bringup.c` run the real
firmware over it.

## Memory map

The Falcon gives P, X and Y separate external RAM halves and folds X into the
upper half of the same array. The Intellifex board has one 64K-word RAM that P,
X and Y all address at the same word addresses, so a word written as `P:$0F80`
is read back as `Y:$0F80`.

- `dsp_core.h`: `DSP_RAMSIZE` (32768) becomes `DSP_EXTSIZE` (0x10000), and
  `ramext` grows to match.
- `dsp_cpu.c`: `read_memory()`, `read_memory_p()` and `write_memory_raw()`
  index `ramext` with the address unchanged instead of masking it and setting
  the X offset.

Internal memory stays as upstream: 512 program words, 256 words each of X and
Y, and the `$0100..$01FF` data ROMs only while `OMR.DE` is set. X and Y space
still answer from their peripheral arrays at `$FFC0` and above — the firmware
keeps everything below that in P and Y, and only X has side effects there.

## One word at a time

`dsp56k_write_memory()` and `dsp56k_read_memory()` expose a single word
through the same path an instruction takes, peripheral side effects included.
The board uses the write side to load the boot overlay; the check tool uses
both. `dsp_cpu.h` and `dsp_disasm.h` gained `<stdint.h>`, `<stdio.h>` and the
prototypes, which upstream got from `main.h`.

## SSI frame sync

`dsp_core_ssi_Receive_SC1()` and `_SC2()` set RFS and TFS on the frame sync
edge in normal mode as well as network mode, and clear `waitFrameRX` and
`waitFrameTX` only when the edge asserts. Upstream ties both to network mode,
which leaves a receiver that the firmware enables mid-frame waiting for a
transition that never comes, and the firmware never runs network mode.

## Build glue

- `ifx_hatari_glue.h` stands in for `main.h`, `log.h`, `debugui.h`,
  `profile.h`, `ioMem.h` and `dsp.h`: tracing and the profiler are compiled
  out, `Log_Printf()` writes to stderr, and the crossbar/DMA hooks do nothing.
- The three `.c` files silence `-Wconversion` and `-Wsign-conversion` around
  themselves: Hatari keeps DSP values in wide unsigned types and truncates
  them on purpose, and the casts that would say so bury the emulation.
- `dsp_cpu.c` marks the interrupt case that executes a second instruction as
  `__attribute__((fallthrough))`.
- `dsp_disasm.c` adds `dsp56k_getInstructionOnlyText()`, which
  `../ifx_dsp_disasm.c` needs to print a mnemonic without operands.

These files keep upstream's formatting; `make format` and the static checks
only cover the harness sources around them.
