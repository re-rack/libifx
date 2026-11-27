# Intellifex LTD effects

A C17 library, `libifx`, that re-creates the six effect configurations of the Rocktron Intellifex
LTD (1995) for use as audio plugins, bit-exact against the original firmware: for the same input
and knob settings, the same output samples as the hardware's DSP.

**Status: complete.** All six configurations are ported and checked against fixtures recorded from
the original firmware, which a two-CPU harness runs. The effects share the DSP's front, Hush,
mixer, ducker and memory wipe, and their families share the regenerating delay lines and the
chorus, pitch and reverb networks.

| Configuration | Effect | Status |
|---|---|---|
| Hush; Reverb | `ifx_reverb` | complete |
| Hush; Delay; Ducker | `ifx_delay` | complete |
| Hush; 8 Voice Chorus; Delay | `ifx_chorus8` | complete |
| Hush; Pitch Shift; Delay | `ifx_pitch` | complete |
| Hush; Chorus; Delay; Reverb | `ifx_chorus_reverb` | complete |
| Hush; Pitch Shift; Delay; Reverb | `ifx_pitch_reverb` | complete |

## Using it in a plugin

- Run the library at the device's own sample rate, `IFX_DEVICE_RATE` (39 kHz), and resample to and
  from the host's rate. The library takes no rate: every time in the firmware is a fixed number of
  samples, so audio fed at another rate plays as if the device's clock ran at that rate.
- Feed it one mono input, as the device's single A/D does, and take its stereo output, one
  32-frame block later. `src/ifx.h` states the contract every effect follows.
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
| `make check-fixture FIXTURE=<name>`, `make check-tables`, `make check-host` | one part of `make check` |
| `make check-install` | build a C and a C++ host against a staged install |
| `make check-convert` | the float converter on every float, every SIMD path |
| `make record-fixture FIXTURE=<name>`, `make record-all-fixtures` | record fixtures from the firmware harness (fetches the EPROM) |
| `make inputs` | download the EPROM and the manual into `reverse_engineering/inputs/` unless present |
| `make sanitize`, `make coverage` | the checks under ASan and UBSan; gcov coverage of `src/` |
| `make benchmark-all-fx`, `make benchmark-fx FX=<name>` | processing time per frame |
| `make ghidra-prepare` | fetch and verify the EPROM, split it, disassemble both processors and build the Ghidra project |
| `make ghidra-clean` | remove what `ghidra-prepare` generates, keeping the Ghidra project |
| `make harness`, `make tools`, `make dsp-core-check` | the firmware harness, its programs and the DSP core's memory-map check (`reverse_engineering/README.md`) |
| `make format` | apply clang-format |
| `make static-check` | clang-format check, cppcheck, gcc and clang warnings and analyzers, ruff |
| `make clean` | remove `build/` |

The targets that need the EPROM download it, and the manual, from the Internet Archive into
`reverse_engineering/inputs/`. A copy of either in the project root or `reverse_engineering/inputs/`,
under any name, is found by SHA-256 and used instead ([`reverse_engineering/README.md`](reverse_engineering/README.md)).

## Dependencies

- `make`, `gcc` and `clang` (C17), `clang-format`, `cppcheck`
- `check-convert`: `aarch64-linux-gnu-gcc`, `riscv64-linux-gnu-gcc`, `qemu-aarch64`, `qemu-riscv64`; `coverage`: `gcov`
- `python3`, `ruff`: the workspace scripts
- Ghidra 12.2 with Ghidra-Rare's DSP56000 module ([aleph-void/Ghidra-Rare](https://github.com/aleph-void/Ghidra-Rare)) and PyGhidra

## Layout

| Path | Contents |
|---|---|
| `src/` | the library |
| `tests/` | the recorder, the checker and the fixtures ([`tests/README.md`](tests/README.md)) |
| `reverse_engineering/` | the workspace scripts, the harness and the firmware map ([`EFFECTS.md`](reverse_engineering/EFFECTS.md)) |
| `AGENTS.md` | the project's rules and decisions |
