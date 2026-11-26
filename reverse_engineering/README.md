# Reverse-engineering workspace

The repository holds neither the EPROM nor the manual, nor any analysis output that carries the
firmware's bytes. `make ghidra-prepare` builds the workspace locally into gitignored paths:

1. `build_workspace.py` finds the EPROM (and the manual) by SHA-256, under any name, in the project
   root or `reverse_engineering/inputs/`, re-derives the EPROM's layout from the DSP image headers
   and checks it, splits the EPROM into records and checks each one's SHA-256, disassembles both
   processors and writes the memory map, entry points, call graph and Ghidra seeds.
2. `build_ghidra.py --prepare build/ghidra` writes the Ghidra programs' images for an import by hand.
3. `build_ghidra.py` builds the analysed Ghidra project, once: an existing project is kept, since it
   may hold your own analysis.

| Path | Content |
|---|---|
| `firmware/` | the records and `manifest.csv` (below) |
| `asm/mcu.asm` | the 8051 program, `C:0000..4AEF`: decoded instructions where the recursive descent reaches, `db` lines with ASCII elsewhere |
| `asm/dsp_boot.asm`, `asm/dsp_shared.asm` | the bootstrap and shared records disassembled at their load addresses |
| `asm/dsp_cfg<N>.asm` | configuration N's runtime: bootstrap `P:$0000..$009F`, the configuration and the shared code |
| `memory_map.csv` | one row per record, image header and fill: region, processor, programs, kind, EPROM offset, load address, size |
| `entrypoints.csv` | vectors, handlers and cross-image entries with the evidence for each |
| `callgraph/direct_calls.csv` | every direct call site and its target |
| `callgraph/functions.csv` | call targets with their caller count |
| `callgraph/direct_calls.dot` | the same graph as Graphviz source |
| `seeds.csv` | entry points, record starts and direct branch and call targets per program |
| `ghidra/Intellifex.gpr` | the Ghidra project |
| `ghidra_analysis/` | functions, calls and blocks exported from it |
| `../build/ghidra/` | the programs' images, with `IMPORT.txt` |

`make ghidra-clean` removes everything but the Ghidra project and `ghidra_analysis/`, which only
building a project writes. To rebuild the project, delete `ghidra/` and `ghidra_analysis/` yourself,
or build another beside it:
`~/.local/share/gmajor2/pyghidra/bin/python reverse_engineering/build_ghidra.py Intellifex-fresh`.

## Tools

- `python3`, and a C compiler for `build/tools/ifx_dsp_disasm`, which the Makefile builds from the
  harness's copy of Hatari's DSP56001 disassembler (`DISASSEMBLER=` names another; it must accept
  `-f u24be --color never -s HEX -n COUNT FILE`).
- Ghidra 12.2 (`GHIDRA=`, default `/opt/ghidra`) with Ghidra-Rare's DSP56000 module from
  [aleph-void/Ghidra-Rare](https://github.com/aleph-void/Ghidra-Rare) (the release's
  `ghidra_12.2_PUBLIC_*_DSP56000.zip` unpacked into `Ghidra/Extensions`), and a Python with
  PyGhidra (`PYGHIDRA=`, default `~/.local/share/gmajor2/pyghidra/bin/python`). Step 3 is skipped
  without them. uv installs PyGhidra from Ghidra's bundled offline package:

```sh
uv venv ~/.local/share/gmajor2/pyghidra
uv pip install --python ~/.local/share/gmajor2/pyghidra/bin/python --no-index \
  -f /opt/ghidra/Ghidra/Features/PyGhidra/pypkg/dist pyghidra
```

## The records

| File | Processor | Load address | Content |
|---|---|---|---|
| `00_mcu_0000_19184b.bin` | 8051 | `C:0000` | MCU program and data, EPROM `0x0000..0x4AEF` |
| `01_dsp_boot_p0000_211w.bin` | DSP56001 | `P:$0000` | bootstrap |
| `02_dsp_shared_p0400_1552w.bin` | DSP56001 | `P:$0400` | shared code |
| `03_dsp_cfg2_p00a0_602w.bin` | DSP56001 | `P:$00A0` | configuration 2, Hush; Reverb |
| `04_dsp_cfg4_p00a0_363w.bin` | DSP56001 | `P:$00A0` | configuration 4, Hush; 8 Voice Chorus; Delay |
| `05_dsp_cfg5_p00a0_357w.bin` | DSP56001 | `P:$00A0` | configuration 5, Hush; Pitch Shift; Delay |
| `06_dsp_cfg3_p00a0_263w.bin` | DSP56001 | `P:$00A0` | configuration 3, Hush; Delay; Ducker |
| `07_dsp_cfg0_p00a0_534w.bin` | DSP56001 | `P:$00A0` | configuration 0, Hush; Chorus; Delay; Reverb |
| `08_dsp_cfg1_p00a0_545w.bin` | DSP56001 | `P:$00A0` | configuration 1, Hush; Pitch Shift; Delay; Reverb |

Records are numbered in EPROM order and hold payloads only: the image headers stay out, and DSP
words stay 24-bit big-endian. `manifest.csv` gives each record's EPROM offset, header offset, space,
load address, unit, count, bytes and SHA-256. `reverse_engineering/EFFECTS.md` explains the layout.

## Programs

Every CSV names the programs an address belongs to, as each CPU holds them:

| Program | Holds |
|---|---|
| `mcu` | the 8051's code space, the whole EPROM |
| `dsp_boot` | the bootstrap alone, `P:$0000..$00D2` |
| `dsp_cfg0` .. `dsp_cfg5` | bootstrap `P:$0000..$009F`, configuration N at `P:$00A0`, shared code at `P:$0400` |

A shared-code address belongs to all six `dsp_cfgN`; a bootstrap address below `$00A0` to
`dsp_boot` and all six; one at `$00A0` or above to `dsp_boot` alone. Never import two images that
load at the same address into one Ghidra program. Cells that need several values join them with
`;`; `seeds.csv` truncates long `source` lists to three entries plus `+N_more`.

## Disassembly

- **8051.** `mcs51_disasm.py` decodes by recursive descent from the reset, timer-0 and serial
  vectors, never linearly (`0xFF` fill decodes as `mov R7,A`). A `jmp @A+DPTR` whose block loads
  DPTR with the table address resolves to its AJMP or LJMP table, sized by a preceding
  `anl A,#mask` where there is one; the entries are instructions too. Any other computed jump is
  reported, and code reached only through one stays undecoded.
- **DSP.** `ifx_dsp_disasm` decodes linearly in DSP56001 syntax: peripherals print as `x:$ffeb`,
  a `do` names its loop's last word, and inline tables appear as junk instructions.

## Call graph limits

`callgraph/` is a candidate direct-call graph: 8051 `lcall`/`acall`, DSP `jsr`, `jscc`, `jsclr` and
`jsset` with an absolute target. Computed calls and jumps are not resolved, code inside data is not
analysed, and a call target is a hint, not a proven function boundary. A DSP call from the
bootstrap or the shared code into `$00A0..$03FF` resolves to `configuration`: which one depends on
the runtime.

## The Ghidra project

`ghidra/Intellifex.gpr` holds eight programs, built with PyGhidra alone:

- `ifx_mcu`: language `8051:BE:16:default`, the whole EPROM at `CODE:0000`, with `0x0000..0x4AEF`
  executable and the DSP images in a non-executable block.
- `ifx_dsp_boot` and `ifx_dsp_cfg0` .. `ifx_dsp_cfg5`: language `DSP56000:BE:16:default`, with
  separate P, X and Y spaces addressed in words. The language holds each 24-bit word in a 4-byte
  big-endian unit, so `build_ghidra.py` widens the records' words. A configuration program is one
  P block for the bootstrap prefix and the configuration and one for the shared code; X and Y are
  uninitialised 64K-word blocks. Ghidra cannot alias them with P as the board does.
- The language names the DSP56001's vectors and peripherals and models its address arithmetic,
  stack and loops in p-code, so the decompiler is usable; Hatari is still the oracle.

Shared code appears in all six configuration programs; annotate it in one. Analysis coverage is not
complete: computed jumps and inline tables are not resolved.

## The harness

`harness/` runs the firmware: the test recorder drives it, and nothing in `src/` links it. `make
harness` builds `build/harness/libifxharness.a`, `make tools` the programs below.

| Path | Content |
|---|---|
| `hatari/` | Hatari's DSP56001 interpreter and disassembler (GPL-2.0-or-later), with the board's memory map; its `README.md` lists every change |
| `mcs51/` | mcs51_emulator (MIT) for the 80C32, with the core bugs a lockstep run against ucsim `s51` found fixed; its `README.md` lists them |
| `ifx_dsp_board.{c,h}` | the DSP side: the core, the host interface, the SSI and the port registers |
| `ifx_mcu_board.{c,h}` | the MCU side: battery RAM, the host port, the panel's knobs and buttons, the display |
| `ifx_emu.{c,h}` | the coupled board: power on, recall, panel edits one detent at a time, a parameter sent through the MCU's own `C:0EE0` without advancing audio, block alignment, a run started at rest from any knob vector (`ifx_emu_load`, `ifx_emu_settle`), audio in and out, the log of parameter messages |
| `ifx_mcu_bringup.c` | `build/tools/ifx_mcu_bringup`, a command line over `ifx_emu` (its file comment lists the commands; `verify:audio`, `verify:echo`, `verify:factory` and `verify:timing` are its checks) |
| `ifx_dsp_disasm.c` | `build/tools/ifx_dsp_disasm`, the workspace's DSP disassembler |
| `ifx_dsp_core_check.c` | `make dsp-core-check`: the vendored core's memory map |

- **Schedule.** One frame is one input sample: 423 DSP instruction cycles (a 33 MHz DSP56001) and
  a fixed number of 80C32 clocks. The firmware's own timing is asynchronous; this schedule is ours.
- **Converters.** The A/D hands the receive register 16 bits; `ifx_emu_feed.adc16 = false` puts
  the whole 24-bit word into it instead, past the SSI's 16-bit word length, which the recorder uses.
  Output is the DSP's full 24-bit transmit words, of which the D/A keeps the top 16 bits.
- **Timing.** `verify:timing` checks the rules the recorder relies on, in all six configurations:
  output trails input by exactly 64 frames, and a message sent after input frame n − 1 is drained
  by the pass that processes the block containing frame n, at every block phase.
- **Sending.** `ifx_emu_send` runs `C:0EE0` as a call from the MCU's idle point in its main loop
  (`C:0140`) and restores the MCU's registers afterwards; the DSP runs alongside with its SSI
  frames held, so audio does not advance.
- **Setup words.** `ifx_emu_apply` feeds silence until the next pass has run its dispatcher and
  stops at its return, before the configuration can change what the handlers staged.
- **Starting at rest.** `ifx_emu_load` writes a user preset, clears the DSP's memory, reboots the
  board and recalls the preset through the panel; `ifx_emu_settle` then feeds silence until the
  wipe and initialisation are done and the output has rested on `$066666`.
- **Battery.** Power-on starts from the battery the firmware's own FACTORY RESTORE leaves:
  `ifx_emu_battery_factory()` builds the same image `ifx_emu_factory_restore()` gets by dialling
  the panel, which `verify:factory` checks byte for byte.
- One DSP board per process: the vendored core keeps global state.

