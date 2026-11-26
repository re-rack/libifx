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

- `python3` and `dsp56300-disasm` from [xemu-project/dsp56300](https://github.com/xemu-project/dsp56300)
  (`DISASSEMBLER=` names another; it must accept `-f u24be --color never -s HEX -n COUNT FILE`).
- Ghidra 12.2 (`GHIDRA=`, default `/opt/ghidra`) with the Ghidra-Rare DSP56300 extension from
  [aleph-void/ghidra-dsp56300](https://github.com/aleph-void/ghidra-dsp56300), and a Python with
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
- **DSP.** `dsp56300-disasm` decodes DSP56300 encodings, a superset of the DSP56000's, linearly:
  inline tables appear as junk instructions, and peripheral addresses print as `x:$ffffeb` for the
  DSP56001's `X:$FFEB`.

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
- `ifx_dsp_boot` and `ifx_dsp_cfg0` .. `ifx_dsp_cfg5`: language `DSP56300:LE:24:default`. The
  language reads byte-reversed LE24 words and addresses bytes, so `build_ghidra.py` reverses each
  word and places word address W at byte 3W. A configuration program is one block for the
  bootstrap prefix and the configuration and one for the shared code.

Shared code appears in all six configuration programs; annotate it in one. Analysis coverage is not
complete: computed jumps and inline tables are not resolved.
