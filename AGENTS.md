# Rocktron Intellifex LTD Firmware Reverse Engineering and Native Effects

## Project goal

This project has two connected goals:

1. Reverse engineer the Rocktron Intellifex LTD v1.0 firmware (an 8051 host MCU that loads and drives a Motorola DSP56001) to recover its effect algorithms, parameter setup, arithmetic and state behaviour.
2. Reimplement every configuration as a reusable native C17 library, `libifx`, one at a time, rather than emulating the whole device.

The original firmware, run routine by routine in an emulator harness, is the reference oracle. Native code is bit-exact by default: coefficient generation, arithmetic order, rounding, saturation, wrapping and persistent state. Static analysis and emulation support the library; they are not the deliverable.

`/home/cosku/Documents/libgmajor2` did the same for the TC Electronic G-Major 2 and is the template for method, layout, conventions and generic code. Treat it as read-only, and never assume its G-Major findings hold here: its addresses, rate words, job lists, software float and block levels are specific to that device.

## Inputs

| File | Content |
|---|---|
| the EPROM, 32,768 bytes, SHA-256 `7f6162d9e77e9b63783df7a8158c646d287e746de1ca56bdbea63b92e60004b1` | the 27C256 of firmware v1.0, January 1995 |
| the manual, SHA-256 `bda911de8908853575a79bcaae32d5dfd99ec52ca7656b004df992f7ee16034e` | the 1995 user's manual, 60 pages |

The scripts find both by SHA-256, under any name, in the project root or `reverse_engineering/inputs/`. Never commit either, or anything that carries the firmware's bytes: records, disassembly, LE24 copies, the Ghidra project. `.gitignore` keeps them out.

## Reference documentation

- The manual describes the six configurations and their signal flow (printed pages 22–35), every section's parameters and ranges (pages 11–21), the factory presets (page 55) and the specifications (page 57). Treat it as a behavioural reference, never as proof of firmware layout or arithmetic.
- Printed page N is PDF page N+3.
- Text: `pdftotext -layout intellifex_ltd.pdf /tmp/ifx_manual.txt`. The signal-flow diagrams are images: `pdftoppm -png -r 150 -f N -l N intellifex_ltd.pdf /tmp/page`.
- Regeneration Limiting (page 12) arrived in software 1.01; this EPROM is 1.0. Check whether a feature exists in the firmware before porting it.

## Layout

| Path | Purpose |
|---|---|
| `reverse_engineering/` | Workspace scripts and docs; `make ghidra-prepare` writes the records, disassembly, call graph, seeds and Ghidra project under it, all untracked (`reverse_engineering/README.md`) |
| `reverse_engineering/build_workspace.py` | Verify the inputs, split the EPROM into records, disassemble both processors and write the analysis artifacts |
| `reverse_engineering/mcs51_disasm.py` | The 8051 recursive-descent disassembler the workspace uses |
| `reverse_engineering/build_ghidra.py` | Build and export the Ghidra project with PyGhidra only |
| `reverse_engineering/EFFECTS.md` | Verified firmware map and reusable findings for the native library |
| `Makefile` | Every build, check and tool target; all output goes to `build/` or the workspace's untracked paths |
| `README.md` | The project's front page |

`src/` (library code only), `tests/` (recorder, checker, fixtures) and `reverse_engineering/harness/` (emulator plumbing) follow the rules below once they exist.

## Native effect progress

An effect is **Complete** when it has a native setup path, an audio kernel, all four process entry points and a recorded fixture that passes `make check`. Identified addresses or partly understood code do not count.

| Configuration (manual pages) | Effect | Sections | Status |
|---|---|---|---|
| Hush; Reverb (25–26) | `ifx_reverb` | Mixer, Hush, Reverb with 8 types, pre-delay and gate | Not started |
| Hush; Delay; Ducker (27–29) | `ifx_delay` | Mixer, Hush, Delay (Stereo, Ping-Pong, 2-Tap), Ducker | Not started |
| Hush; 8 Voice Chorus; Delay (30–31) | `ifx_chorus8` | Mixer, Hush, 8 chorus/delay voices | Not started |
| Hush; Pitch Shift; Delay (32–33) | `ifx_pitch` | Mixer, Hush, 4 pitch voices with delay | Not started |
| Hush; Chorus; Delay; Reverb (23–24) | `ifx_chorus_reverb` | Mixer, Hush, 4 chorus/delay voices, Ducker, Reverb | Not started |
| Hush; Pitch Shift; Delay; Reverb (34–35) | `ifx_pitch_reverb` | Mixer, Hush, 2 pitch voices, Delay 3, Ducker, Reverb | Not started |

Work order: workspace and docs (done), firmware map, two-CPU harness, library foundation (build, converter, process macro, DSP56000 arithmetic, test framework, signal generator, tables fixture), then the configurations simplest first, with Hush and the Mixer as the first shared pieces, then de-popped variants where the firmware clicks.

MIDI, preset storage, title edit, controller and program mapping, bypass and the panel are supporting firmware areas, not effects.

## Decisions

Agreed with the user; change them only with the user.

- **Name.** `libifx`, prefix `ifx_` / `IFX_`, GPL-3.0 like libgmajor2.
- **Unit of an effect.** One effect per configuration: each configuration runs one DSP program whose sections share regeneration loops, the ducker and the mixer. Reverb type and delay type are knobs. Code several programs share becomes a family core (`ifx_<family>_core`) and shared setup math a family design (`ifx_<family>_design`).
- **Sample rate.** The firmware has no rate math: every time is a fixed sample count and the SSI runs on external clocks. The library runs only at the device rate, which a constant names, and is bit-exact there; a host at another rate resamples. There is no rate and no context in the API. The exact rate is set by the codec's clock and is still being pinned (`reverse_engineering/EFFECTS.md`, "Sample rate").
- **Channels and the dry path.** The DSP sees one mono input, the analog sum of the two input jacks, and writes stereo. Each effect is the DSP: mono in, stereo out, Direct Post included, bit-exact. How the analog front end forms the mono input is documented, not ported. A small helper applies the device's L/R Direct Level steps, as ideal gains, to the host's stereo dry signal for Direct Pre, which on the device never enters the DSP.
- **Harness cores.** DSP: the xemu DSP56300 core with shims for every DSP56000 behaviour the firmware relies on, cross-checked against Hatari's DSP56001 core. MCU: `mcs51_emulator` (MIT, C11, <https://github.com/martinribelotta/mcs51_emulator>, upstream commit `8789c25`), vendored under `reverse_engineering/harness/mcs51/` with its licence, patched only to build under the project's warning set, and cross-checked once against ucsim `s51` traces of the firmware's boot.
- **Ghidra layout.** One program per DSP runtime (boot words `P:$0000..$009F`, one configuration image at `P:$00A0`, the shared image at `P:$0400`), a boot-only program, and the MCU program. Shared code is duplicated across the runtime programs: annotate it in one of them.
- **Processing block.** Fixture turns are offset so they land on every frame of the firmware's processing block.

## Terminology

- **Configuration**: one of the six DSP programs and its preset type. **Section**: a part of a configuration the manual names (Mixer, Hush, Reverb, Delay, Ducker, Chorus, Pitch Shift).
- **Block**: the firmware's processing unit of frames. **Buffer**: a caller's buffer.
- **Record**: one piece of the split EPROM (`reverse_engineering/firmware/`). **Runtime**: what the DSP holds while a configuration runs.

## Library API

Every effect has the same interface, the instance first and the knob struct by const pointer; `src/ifx.h` will state the contract once and include every effect inside `extern "C"`:

```c
int  ifx_X_setup_config(const ifx_X_setup_raw *raw, ifx_X_config *config);
void ifx_X_state_init(ifx_X_state *state, const ifx_X_config *config);
void ifx_X_process_split(ifx_X_state *state, const ifx_X_config *config, const int32_t *in,
                         int32_t *out_l, int32_t *out_r, size_t frames);
void ifx_X_process_f32(ifx_X_state *state, const ifx_X_config *config, const float *in,
                       float *out_l, float *out_r, size_t frames);
void ifx_X_process_interleaved(ifx_X_state *state, const ifx_X_config *config,
                               const int32_t *in, int32_t *out, size_t frames);
void ifx_X_process_frame(ifx_X_state *state, const ifx_X_config *config, int32_t in,
                         int32_t out[2]);
```

- Setup forces every raw knob into the firmware's own window and returns 0, or -1 for a NULL pointer. A knob change means the host reruns setup and passes the new config, which applies from the next frame; there is no publish or apply call.
- The interleaved entry point takes a mono input buffer and writes interleaved stereo. Any buffer size works, and an output may share its buffer with the input.
- The four entry points are generated from one frame function by `IFX_PROCESS_DEFINE`.
- No allocation and no global state. `make build` writes `build/default/libifx.a` with `-fPIC` and hidden visibility; `make install` installs it with every header under `include/ifx/` and an `ifx.pc`. Headers stay valid C++17: `restrict` and `_Static_assert` stay out of what C++ reads.
- No public function, parameter or constant exists for tests alone.

## Float interface

Samples are Q23, a signed 24-bit value in an `int32_t`. `src/ifx_convert.{h,c}` is libgmajor2's `gm2_convert` with the prefix renamed, and its exhaustive test comes with it: f32 to Q23 scales by 2^23, rounds to nearest even, maps NaN to 0 and saturates to `[-0x800000, 0x7FFFFF]`; Q23 to f32 reads bits 23..0 as signed and scales by 2^-23, exactly. The path is fixed at compile time: AVX-512, AVX2, SSE2, AArch64 NEON, RISC-V V (or Zve32f), else scalar; `-DIFX_NO_SIMD` forces scalar. The float wrapper converts through a fixed stack chunk.

## Code conventions

libgmajor2's `AGENTS.md` "Code conventions" apply with `gm2` renamed `ifx`; the points that differ or matter most:

- An effect X is four files: `ifx_X.h` (config, state, process entry points), `ifx_X.c`, `ifx_X_setup.h` (the raw struct and `ifx_X_setup_config`) and `ifx_X_setup.c`. A shared audio routine is `ifx_<family>_core.{h,c}`, shared setup math `ifx_<family>_design.{h,c}`.
- Everything a header declares starts with the header's module name. Inside a `.c` file, static functions drop the `ifx_` prefix, constants are enum constants without `IFX_`, constant data is `static const ... k_<name>`, and a file that clamps raw knobs names its windows `<FIELD>_MIN` / `<FIELD>_MAX` in one enum.
- A config holds `int32_t coef[]`: the words the firmware stages, in the firmware's order, as sign-extended 24-bit values, with an enum naming the positions the code uses. A state holds the kernel's own words. Nothing is kept because the firmware keeps it: a scratch word is a local, a constant word is a constant, a word no output depends on is not computed. Only audio input is masked on reading.
- DSP56000 arithmetic, firmware tables, LFOs, parameter smoothing and conversions live in shared helpers, never in an effect. The 8051's integer math and tables become shared helpers too, word for word.
- Raw fields are named after the knob in lower case with underscores; their comment gives the range and meaning, never a default.
- A header's file comment says what the module is and which firmware routines it ports. Interface functions carry no comment unless the effect has something of its own to say. A header declares state_init, process_split, process_f32, process_interleaved, process_frame, in that order. A `.c` file orders: includes (its own header first, then the others alphabetically), constants, static helpers, the frame function, state_init, `IFX_PROCESS_DEFINE`.
- Comments carry only a *why* the code cannot show, in the fewest words; never narrate the code or the change.

## Effect independence

Every effect becomes its own plugin: a standalone module with its own config, state and process functions, sharing only the common helpers and its family's core and design. The firmware's routing, bypass, presets and MIDI are not ported.

## Host integration

- **Analog stages are not ported**: the converters, the input and output pots, the reference switch, the analog direct path. Level knobs the firmware applies (the Mixer) are knobs of the effect, and the host adds no blend of its own.
- **Asynchronous timing gets a fixed schedule.** Whatever the firmware times asynchronously (MCU timers, the pace at which the MCU sends words) gets a fixed schedule in the port, and the recorder uses the same schedule. The arithmetic is the firmware's; only the timing is ours.
- **Knob turns follow the device**: each word glides, ramps or jumps exactly as the firmware moves it.
- **Effects start at rest**: buffers and detectors empty, smoothed words on their targets. A parameter that no page of the configuration shows is not ported. No features the device lacks (tempo sync, LFO alignment) unless the user asks.

## Testing

The emulator only records; every check runs offline.

- `tests/ifx_record.c` runs the firmware in the two-CPU harness and writes `tests/fixtures/<name>.bin`; `tests/ifx_check.c` replays every fixture through the public API alone. Expected values come only from the firmware, and test code never enters `src/`.
- An effect is described once: a knob table and native adapter in `tests/effect_<name>.c`, a firmware adapter in `tests/firmware_<family>.c`, and one line in the effects list.
- A fixture holds: setup words for each knob on its own (every position for 32 or fewer, otherwise 32 evenly spread positions plus the default); four multi-knob presets (every knob at 33 %, 66 %, minimum, maximum, each built one knob at a time) plus the configuration's factory presets; one long audio run on the generated input (libgmajor2's `gm2_signal.c`), moving each knob through min, 33 %, 66 %, max and default, sweeping every knob and giving each ten fast turns 64 frames apart that land while the previous glide still moves, with segment lengths offset over the processing block, and delay-time knobs holding their maximum until the buffer wraps. Every output sample is stored.
- **The recorder follows the device.** A knob turn is the MCU's own edit routine (the one PARAMETER ADJUST and MIDI CC use) followed by the DSP's handling of what the MCU sends. An adapter never copies staged words onto live ones and never takes a value from the library.
- **The checker** replays every run through all four entry points with random buffer sizes and junk in the unused upper input bytes, checks that setup at min-1 and max+1 equals setup at min and max, and fills configs with junk before setup. A tables fixture holds every lookup table as the EPROM holds it. Internal state is not compared.
- Make targets as in libgmajor2: `build`, `install`, `check-install`, `check`, `check-fixture`, `record-fixture`, `record-all-fixtures`, `coverage`, `benchmark-all-fx`, `benchmark-fx`, `check-convert`, `format`, `static-check`, `sanitize`, `clean`, `ghidra-prepare`, `ghidra-clean`. Every C file compiles with one warning set (libgmajor2's `STRICT`), warnings are errors, and all output goes to `build/`.
- After adding an effect, run `coverage` and confirm that a deliberately broken constant makes `check` fail. Re-record only when an effect's description, the input or the fixture format changes, never because `src/` changed.

**Before every commit**, `make format`, `make static-check`, `make sanitize`, `make check` and `make check-install` must all pass with no findings (each target that exists so far).

## Workspace invariants

- The EPROM holds the 8051 program at `0x0000..0x4AEF`, then eight DSP images, then `0xFF` fill from `0x7EEF` (`reverse_engineering/EFFECTS.md`, "EPROM layout"). `build_workspace.py` re-derives this from the image headers and checks every record's SHA-256.
- DSP words are 24-bit big-endian in the EPROM and the records. Ghidra's DSP56300 language reads byte-reversed LE24 words and uses byte addresses, three per word: `build_ghidra.py` reverses the words in memory and multiplies word addresses by three.
- All six configuration images load at `P:$00A0`, over the boot image's init code. Never put two images that load at the same address into one Ghidra program.
- `dsp56300-disasm` decodes DSP56300 encodings, a superset of the DSP56000's, and prints the DSP56001's peripheral addresses `X:$FFC0..$FFFF` as `x:$ffffc0..`. An instruction only the DSP56300 has (`bsr`, `bra`, `brclr`...) in this code means data.

## Tooling

- **xemu DSP56300 toolkit** (<https://github.com/xemu-project/dsp56300>, MIT): `dsp56300-disasm`, `dsp56300-asm` and the C emulator library (`/usr/local/include/dsp56300.h`, link `-ldsp56300_emu_ffi -ldl -lpthread -lm`). It does not implement Sixteen-bit Compatibility mode (`/usr/local/share/doc/dsp56300/LIMITATIONS.md` §1.2), so the harness must reproduce every DSP56000 behaviour the firmware depends on: 16-bit address arithmetic and modulo addressing (N's sign is bit 15, `M=$FFFF` is linear), loop count 0 running 65,536 times, the 15-entry hardware stack, the DSP56000 SR and OMR layouts, two-word vectors at `P:$0000..$003F` including the host-command vectors, peripherals at `X:$FFC0..$FFFF`. The on-chip data ROMs are off (the firmware clears OMR and never sets DE). Verify every instruction's semantics against the DSP56000 Family Manual, not the DSP56300's.
- **Hatari** (`hatari` in Arch `extra`, GPL-2.0-or-later): its DSP56001 core is an independent cross-check, used as a tool, never linked into anything distributed.
- **ucsim `s51`** (in `sdcc`, Arch `extra`): the reference for the one-time 8051 core cross-check.
- **Ghidra 12.2** at `/opt/ghidra` with the built-in 8051 language (`8051:BE:16:default`) and the Ghidra-Rare DSP56300 extension (`DSP56300:LE:24:default`, <https://github.com/aleph-void/ghidra-dsp56300>). PyGhidra only: the environment at `~/.local/share/gmajor2/pyghidra/bin/python`; no Java scripts and no `analyzeHeadless` wrappers.
- Python scripts use the standard library only and stay clean under `ruff check` and `ruff format --check` (`make static-check`).

## Boundaries and safeguards

- `make ghidra-clean` removes the generated workspace and keeps the Ghidra project. Never overwrite or delete a project that may hold the user's annotations without asking; `build_ghidra.py` refuses to.
- Direct call graphs omit computed calls and jumps; candidate function targets are not proven function boundaries.
- `EFFECTS.md` holds verified addresses and reusable facts only. Every doc describes the latest state, never its history.
