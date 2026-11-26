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

The scripts find both by SHA-256, under any name, in the project root or `reverse_engineering/inputs/`. Never commit either, or anything that carries the firmware's bytes: records, disassembly, the Ghidra images, the Ghidra project. `.gitignore` keeps them out.

## Reference documentation

- The manual describes the six configurations and their signal flow (printed pages 22–35), every section's parameters and ranges (pages 11–21), the factory presets (page 55) and the specifications (page 57). Treat it as a behavioural reference, never as proof of firmware layout or arithmetic.
- Printed page N is PDF page N+3.
- Text: `pdftotext -layout intellifex_ltd.pdf /tmp/ifx_manual.txt`. The signal-flow diagrams are images: `pdftoppm -png -r 150 -f N -l N intellifex_ltd.pdf /tmp/page`.
- The manual dates Regeneration Limiting (page 12) to software 1.01, but this 1.0 EPROM's DSP code has it (`EFFECTS.md`, "Configurations"), so it is ported. Check in the firmware whether any feature exists before porting it.

## Layout

| Path | Purpose |
|---|---|
| `reverse_engineering/` | Workspace scripts and docs; `make ghidra-prepare` writes the records, disassembly, call graph, seeds and Ghidra project under it, all untracked (`reverse_engineering/README.md`) |
| `reverse_engineering/build_workspace.py` | Verify the inputs, split the EPROM into records, disassemble both processors and write the analysis artifacts |
| `reverse_engineering/mcs51_disasm.py` | The 8051 recursive-descent disassembler the workspace uses |
| `reverse_engineering/build_ghidra.py` | Build and export the Ghidra project with PyGhidra only |
| `reverse_engineering/harness/` | The two-CPU harness the recorder drives: the vendored cores, the DSP and MCU boards, the coupled board `ifx_emu`, the bring-up CLI and the DSP disassembler (`reverse_engineering/README.md`, "The harness") |
| `reverse_engineering/EFFECTS.md` | Verified firmware map and reusable findings for the native library |
| `Makefile` | Every build, check and tool target; all output goes to `build/` or the workspace's untracked paths |
| `README.md` | The project's front page |
| `src/` | Library code only: `ifx.h`, the base and context, the converter, the process macro (`ifx_process.h`), the DSP56000 arithmetic (`ifx_dsp_math.h`), the shared tables, and the effects |
| `tests/` | The recorder, the checker, the signal generator and the fixtures (`tests/README.md`) |

## Native effect progress

An effect is **Complete** when it has a native setup path, an audio kernel, all four process entry points and a recorded fixture that passes `make check`. Identified addresses or partly understood code do not count.

| Configuration (manual pages) | Effect | Sections | Status |
|---|---|---|---|
| Hush; Reverb (25–26) | `ifx_reverb` | Mixer, Hush, Reverb with 8 types, pre-delay and gate | Complete |
| Hush; Delay; Ducker (27–29) | `ifx_delay` | Mixer, Hush, Delay (Stereo, Ping-Pong, 2-Tap), Ducker | Complete |
| Hush; 8 Voice Chorus; Delay (30–31) | `ifx_chorus8` | Mixer, Hush, 8 chorus/delay voices | Complete |
| Hush; Pitch Shift; Delay (32–33) | `ifx_pitch` | Mixer, Hush, 4 pitch voices with delay | Not started |
| Hush; Chorus; Delay; Reverb (23–24) | `ifx_chorus_reverb` | Mixer, Hush, 4 chorus/delay voices, Ducker, Reverb | Complete |
| Hush; Pitch Shift; Delay; Reverb (34–35) | `ifx_pitch_reverb` | Mixer, Hush, 2 pitch voices, Delay 3, Ducker, Reverb | Not started |

Work order: workspace and docs (done), firmware map (structure done; the arithmetic is settled in the harness), two-CPU harness (done), library foundation (done), then the configurations simplest first, with Hush and the Mixer as the first shared pieces. No de-popped variants: each effect clicks where the device does.

MIDI, preset storage, title edit, controller and program mapping, bypass and the panel are supporting firmware areas, not effects.

## Decisions

Agreed with the user; change them only with the user.

- **Name.** `libifx`, prefix `ifx_` / `IFX_`, GPL-3.0 like libgmajor2.
- **Unit of an effect.** One effect per configuration: each configuration runs one DSP program whose sections share regeneration loops, the ducker and the mixer. Reverb type and delay type are knobs. Code several programs share becomes a family core (`ifx_<family>_core`) and shared setup math a family design (`ifx_<family>_design`).
- **Sample rate.** The firmware has no rate math: every time is a fixed sample count and the SSI runs on external clocks. The library is bit-exact at the device's own rate, and a host at another rate resamples to it. The rate is an argument of the library's context init, 39,000 Hz by default (`IFX_DEVICE_RATE`), the rate the firmware's millisecond conversions assume; the exact rate is the A/D's CLKIN/128 and not yet known (`reverse_engineering/EFFECTS.md`, "Sample rate"). The rate tells the host what to resample to; no arithmetic depends on it.
- **Channels and the dry path.** The DSP sees one mono input, the analog sum of the two input jacks, and writes stereo. Each effect is the DSP: mono in, stereo out, Direct Post included, bit-exact. How the analog front end forms the mono input is documented, not ported. Direct Pre, the analog dry path that never enters the DSP, is not ported either, and the library has no helper for it: with DIRECT HUSH at PRE an effect outputs the wet signal only, and a host adds its own dry signal.
- **Harness cores.** DSP: Hatari's DSP56001 interpreter (`src/falcon/dsp_cpu.c`, `dsp_core.c`, `dsp_disasm.c`; GPL-2.0-or-later, <https://github.com/hatari/hatari>, upstream commit `36a8acf`), vendored under `reverse_engineering/harness/hatari/` with the board's memory and peripheral glue in place of the Falcon's. It implements the DSP56000 behaviour the firmware relies on that the xemu DSP56300 core cannot (multiple wrap-around modulo jumps, 16-bit AGU, the 15-entry stack, the 56001 SR and vectors), and its disassembler is the workspace's DSP disassembler. MCU: `mcs51_emulator` (MIT, C11, <https://github.com/martinribelotta/mcs51_emulator>, upstream commit `8789c25`) in its 8052 model (the board's 80C32 has 256 bytes of IRAM, which the parameters use), vendored under `reverse_engineering/harness/mcs51/` with its licence, patched only to build under the project's warning set, and cross-checked once against ucsim `s51` traces of the firmware's boot.
- **Ghidra layout.** One program per DSP runtime (boot words `P:$0000..$009F`, one configuration image at `P:$00A0`, the shared image at `P:$0400`), a boot-only program, and the MCU program. Shared code is duplicated across the runtime programs: annotate it in one of them.
- **Processing block.** Fixture turns are offset so they land on every frame of the firmware's processing block.
- **Output offset.** The DSP's mixer adds a constant `0x066666` (+0.05) to both outputs before its limiter, which the analog output stage's coupling removes on the device. The library outputs the firmware's word minus `0x066666`, saturated to Q23. Fixtures store the firmware's words; the checker subtracts the offset the same way.
- **Converters.** Samples are 24-bit in and out: the DSP's arithmetic on whole Q23 input words and its whole 24-bit output words. The A/D's and D/A's 16-bit resolution is not ported, and the recorder feeds 24-bit words.

## Terminology

- **Configuration**: one of the six DSP programs and its preset type. **Section**: a part of a configuration the manual names (Mixer, Hush, Reverb, Delay, Ducker, Chorus, Pitch Shift).
- **Block**: the firmware's processing unit of frames. **Buffer**: a caller's buffer.
- **Record**: one piece of the split EPROM (`reverse_engineering/firmware/`). **Runtime**: what the DSP holds while a configuration runs.

## Library API

Every effect has the same interface, the instance first and the knob struct by const pointer; `src/ifx.h` will state the contract once and include every effect inside `extern "C"`:

```c
int  ifx_context_init(ifx_context *context, double rate);    /* the device rate, IFX_DEVICE_RATE by default */
int  ifx_X_setup_config(const ifx_context *context, const ifx_X_setup_raw *raw, ifx_X_config *config);
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

- `ifx_context_init` returns 0, or -1 for a NULL context or a rate that is not finite and positive, which leaves the context without a rate. Setup forces every raw knob into the firmware's own window and returns 0, or -1 for a NULL pointer or a context without a rate. A knob change means the host reruns setup and passes the new config, which applies from the next frame; there is no publish or apply call.
- The interleaved entry point takes a mono input buffer and writes interleaved stereo. Any buffer size works. In split and f32 either output may be the input buffer; the interleaved output, twice the input's size, must not overlap it.
- The four entry points are generated from one block function by `IFX_PROCESS_DEFINE`, which gathers and plays the blocks.
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
- A header's file comment says what the module is and which firmware routines it ports. Interface functions carry no comment unless the effect has something of its own to say. A header declares state_init, process_split, process_f32, process_interleaved, process_frame, in that order. A `.c` file orders: includes (its own header first, then the others alphabetically), constants, static helpers, the block function, state_init, `IFX_PROCESS_DEFINE`.
- Comments carry only a *why* the code cannot show, in the fewest words; never narrate the code or the change.

## Effect independence

Every effect becomes its own plugin: a standalone module with its own config, state and process functions, sharing only the common helpers and its family's core and design. The firmware's routing, bypass, presets and MIDI are not ported.

## Host integration

- **Analog stages are not ported**: the converters, the input and output pots, the reference switch, the analog direct path. Level knobs the firmware applies (the Mixer) are knobs of the effect, and the host adds no blend of its own.
- **Asynchronous timing gets a fixed schedule.** Whatever the firmware times asynchronously (MCU timers, the pace at which the MCU sends words) gets a fixed schedule in the port, and the recorder uses the same schedule. The arithmetic is the firmware's; only the timing is ours.
- **Knob turns follow the device**: each word glides, ramps or jumps exactly as the firmware moves it.
- **Blocks and latency.** The firmware works in 32-frame blocks, and a block's Hush gain, ducker and regen limiter depend on that block's own peak, so the port has one block of latency, 32 frames: the state gathers a block, processes it when its last frame arrives, with the config passed with that frame, and plays it during the next 32 frames; the first 32 output frames are the rest output. Port output frame m is the device's output frame m + 32, bit for bit, the device adding 32 frames of converter double buffering. Blocks are counted from state_init.
- **Recorded knob turns** go through the MCU's own send routine `C:0EE0`, the path MIDI controllers use, with the value in the parameter's IRAM slot: one message per change, delivered after the previous block's messages were drained, so that the block containing the turn's frame applies it. The panel path, one message per detent, is checked separately by the harness.
- **Effects start at the configuration's initialisation**: state_init is the device's state when the configuration's init pass processes its first block (buffers and detectors empty, smoothed words on their targets), the point where `ifx_emu_start` starts every recording; nothing in `src/` replays silent blocks to get there. A parameter that no page of the configuration shows is not ported. No features the device lacks (tempo sync, LFO alignment) unless the user asks.

## Testing

The emulator only records; every check runs offline.

- `tests/ifx_record.c` runs the firmware in the two-CPU harness and writes `tests/fixtures/<name>.bin`; `tests/ifx_check.c` replays every fixture through the public API alone. Expected values come only from the firmware, and test code never enters `src/`.
- An effect is described once: a knob table and native adapter in `tests/effect_<name>.c`, a firmware adapter in `tests/firmware_<family>.c`, and one line in the effects list.
- A fixture holds: setup words for each knob on its own (every position for 32 or fewer, otherwise 32 evenly spread positions plus the default); four multi-knob presets (every knob at 33 %, 66 %, minimum, maximum, each built one knob at a time) plus the configuration's factory presets; one long audio run on the generated input (libgmajor2's `gm2_signal.c`), moving each knob through min, 33 %, 66 %, max and default, then from an active setting the effect names per knob (one where every section acts: on, audible, short delays, regen near the rails), once for every value of its mode knobs (delay type, reverb type), sweeping every knob and giving each ten fast turns 64 frames apart that land while the previous glide still moves, with segment lengths offset over the processing block, and delay-time knobs holding their maximum until the buffer wraps. Every output sample is stored.
- **The recorder follows the device.** A knob turn is the MCU's own edit routine (the one PARAMETER ADJUST and MIDI CC use) followed by the DSP's handling of what the MCU sends. An adapter never copies staged words onto live ones and never takes a value from the library.
- **The checker** replays every run through all four entry points with random buffer sizes and junk in the unused upper input bytes, checks that setup at min-1 and max+1 equals setup at min and max, and fills configs with junk before setup. A tables fixture holds every lookup table as the EPROM holds it. Internal state is not compared.
- Make targets as in libgmajor2: `build`, `install`, `check-install`, `check`, `check-fixture`, `record-fixture`, `record-all-fixtures`, `coverage`, `benchmark-all-fx`, `benchmark-fx`, `check-convert`, `format`, `static-check`, `sanitize`, `clean`, `ghidra-prepare`, `ghidra-clean`. Every C file compiles with one warning set (libgmajor2's `STRICT`), warnings are errors, and all output goes to `build/`.
- After adding an effect, run `coverage` and confirm by mutation that the fixture covers every path: a deliberately broken constant, rounding or branch in each new source must make `check` fail. Re-record only when an effect's description, the input or the fixture format changes, never because `src/` changed.

**Before every commit**, `make format`, `make static-check`, `make sanitize`, `make check` and `make check-install` must all pass with no findings (each target that exists so far).

## Workspace invariants

- The EPROM holds the 8051 program at `0x0000..0x4AEF`, then eight DSP images, then `0xFF` fill from `0x7EEF` (`reverse_engineering/EFFECTS.md`, "EPROM layout"). `build_workspace.py` re-derives this from the image headers and checks every record's SHA-256.
- DSP words are 24-bit big-endian in the EPROM and the records. Ghidra's DSP56000 language holds each word in a 4-byte big-endian unit with the high byte zero, at word addresses in separate P, X and Y spaces: `build_ghidra.py` widens the words and replaces the language's default X and Y blocks, which end at `$3FFF`, with full 64K-word ones.
- All six configuration images load at `P:$00A0`, over the boot image's init code. Never put two images that load at the same address into one Ghidra program.
- The DSP listings come from `build/tools/ifx_dsp_disasm` (Hatari's DSP56001 disassembler): a `do` names its loop's last word, and peripherals print as `x:$ffc0..`.

## Tooling

- **Hatari's DSP56001 core** (see Decisions) is the DSP oracle. The firmware depends on: 16-bit address arithmetic and modulo addressing (N's sign is bit 15, `M=$FFFF` is linear), loop count 0 running 65,536 times, the 15-entry hardware stack, the DSP56000 SR and OMR layouts, two-word vectors at `P:$0000..$003F` including the host-command vectors, peripherals at `X:$FFC0..$FFFF`, and the multiple wrap-around rule for an N that is a multiple of the modulo buffer size (DSP56000 Family Manual §4.4.2.2.1). The board's external memory is one array that P, X and Y all address. The on-chip data ROMs are off (the firmware clears OMR and never sets DE). Verify instruction semantics against the DSP56000 Family Manual, not the DSP56300's.
- **xemu DSP56300 toolkit** (<https://github.com/xemu-project/dsp56300>, MIT): `dsp56300-disasm` and the C emulator library (`/usr/local/include/dsp56300.h`) remain available as an ALU cross-check should a Hatari result come into doubt; they do not model the DSP56000 behaviour above.
- **rg-dis** (<https://rg.atari.org/tools/rg-dis/>): another DSP56000 disassembler, should a second opinion be needed.
- **ucsim `s51`** (in `sdcc`, Arch `extra`): the reference for the one-time 8051 core cross-check.
- **Ghidra 12.2** at `/opt/ghidra` with the built-in 8051 language (`8051:BE:16:default`) and Ghidra-Rare's DSP56000 module (`DSP56000:BE:16:default`, DSP56000/56001 with the DSP56002 additions, <https://github.com/aleph-void/Ghidra-Rare>, release v1.1.0, installed under `/opt/ghidra/Ghidra/Extensions/DSP56000`). Its p-code models the DSP56001's 16-bit address arithmetic, modulo with multiple wrap-around, the 15-level stack, `do`/`rep` and the limiter, and its decoding agrees with Hatari's disassembler on every instruction of the seven DSP programs; Hatari stays the oracle. PyGhidra only: the environment at `~/.local/share/gmajor2/pyghidra/bin/python`; no Java scripts and no `analyzeHeadless` wrappers.
- Python scripts use the standard library only and stay clean under `ruff check` and `ruff format --check` (`make static-check`).

## Boundaries and safeguards

- `make ghidra-clean` removes the generated workspace and keeps the Ghidra project. Never overwrite or delete a project that may hold the user's annotations without asking; `build_ghidra.py` refuses to.
- Direct call graphs omit computed calls and jumps; candidate function targets are not proven function boundaries.
- `EFFECTS.md` holds verified addresses and reusable facts only. Every doc describes the latest state, never its history.
