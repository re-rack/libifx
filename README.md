# Intellifex LTD effects

A C17 library, `libifx`, that re-creates the six effect configurations of the Rocktron Intellifex
LTD (1995) for use as audio plugins, bit-exact against the original firmware: for the same input
and knob settings, the same output samples as the hardware's DSP.

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

The code is not a clean-room reverse engineering of the original firmware. Bit-exactness was my aim and that is simply not possible without translating the tables and algorithms verbatim. Many other projects go around this by emulating the whole hardware and pushing the responsibility of finding the firmware to the user. That however was a no-go for me due to very high CPU cost. I think we should be able to conserve vintage hardware sound without needing a beefy computer. This library is very light on resources. Most effects are 100x faster than realtime on my 7800X3D.

Depending on where you live this might be considered a copyright infringement. Eventhough algorithms and math can't be copyrighted, a 1-on-1 translation is still not allowed by many authorities. Methods can be patented though and I have no idea if any of the effects use any kind of patented method.

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