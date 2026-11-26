# Intellifex LTD effects

A C17 library, `libifx`, that is to re-create the six effect configurations of the Rocktron
Intellifex LTD (1995) for use as audio plugins, bit-exact against the original firmware: for the
same input and knob settings, the same output samples as the hardware's DSP.

**Status: four of six effects.** The firmware workspace, the Ghidra project, a two-CPU harness that
runs the original firmware, the library's foundation and test framework, and `ifx_delay`,
`ifx_chorus8`, `ifx_reverb` and `ifx_chorus_reverb` with the shared Hush, mixer, ducker and regen
limiter are in place.

| Configuration | Effect | Status |
|---|---|---|
| Hush; Reverb | `ifx_reverb` | complete |
| Hush; Delay; Ducker | `ifx_delay` | complete |
| Hush; 8 Voice Chorus; Delay | `ifx_chorus8` | complete |
| Hush; Pitch Shift; Delay | `ifx_pitch` | not started |
| Hush; Chorus; Delay; Reverb | `ifx_chorus_reverb` | complete |
| Hush; Pitch Shift; Delay; Reverb | `ifx_pitch_reverb` | not started |

## What a plugin will do

- Run the library at the device's own sample rate (about 39 kHz, a fixed constant) and resample
  to and from the host's rate; the library is bit-exact only there.
- Feed it one mono input, as the device's single A/D does, and take its stereo output.
- Direct Post, the dry signal after the digital Hush, is part of each effect. Direct Pre is the
  device's analog dry path and is not ported: with DIRECT HUSH at PRE the effect outputs only the
  wet signal, and the host mixes in its own dry signal.

## Disclaimer

This is not a clean-room reimplementation: bit-exactness needs the firmware's tables and arithmetic
translated verbatim. Depending on where you live, that may be considered copyright infringement.
The repository holds neither the firmware nor the manual.

## Make targets

| Target | What it does |
|---|---|
| `make build` | `build/default/libifx.a` (the default) |
| `make install`, `make uninstall` | the library, its headers under `include/ifx/` and `ifx.pc`, under `PREFIX` |
| `make check` | every fixture and the host checks, offline ([`tests/README.md`](tests/README.md)) |
| `make check-install` | build a C and a C++ host against a staged install |
| `make check-convert` | the float converter on every float, every SIMD path |
| `make record-fixture FIXTURE=<name>`, `make record-all-fixtures` | record fixtures from the firmware harness (needs the EPROM) |
| `make sanitize`, `make coverage` | the checks under ASan and UBSan; gcov coverage of `src/` |
| `make benchmark-all-fx`, `make benchmark-fx FX=<name>` | processing time per frame |
| `make ghidra-prepare` | verify the EPROM, split it, disassemble both processors and build the Ghidra project |
| `make ghidra-clean` | remove what `ghidra-prepare` generates, keeping the Ghidra project |
| `make harness`, `make tools` | the firmware harness and its programs (`reverse_engineering/README.md`) |
| `make format` | apply clang-format |
| `make static-check` | clang-format check, cppcheck, gcc and clang warnings and analyzers, ruff |
| `make clean` | remove `build/` |

Put the EPROM (and optionally the manual) into the project root or `reverse_engineering/inputs/`
under any name; the scripts find them by SHA-256 ([`reverse_engineering/README.md`](reverse_engineering/README.md)).

## Dependencies

- `make`, `gcc` and `clang` (C17), `clang-format`, `cppcheck`
- `check-convert`: `aarch64-linux-gnu-gcc`, `riscv64-linux-gnu-gcc`, `qemu-aarch64`, `qemu-riscv64`; `coverage`: `gcov`
- `python`, `ruff`: the workspace scripts
- Ghidra 12.2 with Ghidra-Rare's DSP56000 module ([aleph-void/Ghidra-Rare](https://github.com/aleph-void/Ghidra-Rare)) and PyGhidra

## Layout

| Path | Contents |
|---|---|
| `src/` | the library |
| `tests/` | the recorder, the checker and the fixtures ([`tests/README.md`](tests/README.md)) |
| `reverse_engineering/` | the workspace scripts, the harness and the firmware map ([`EFFECTS.md`](reverse_engineering/EFFECTS.md)) |
| `AGENTS.md` | the project's rules and decisions |
