# Intellifex LTD effects

A C17 library, `libifx`, that is to re-create the six effect configurations of the Rocktron
Intellifex LTD (1995) for use as audio plugins, bit-exact against the original firmware: for the
same input and knob settings, the same output samples as the hardware's DSP.

**Status: reverse engineering.** The firmware workspace and Ghidra project build; the firmware map
is in progress and no effect is implemented yet.

| Configuration | Effect | Status |
|---|---|---|
| Hush; Reverb | `ifx_reverb` | not started |
| Hush; Delay; Ducker | `ifx_delay` | not started |
| Hush; 8 Voice Chorus; Delay | `ifx_chorus8` | not started |
| Hush; Pitch Shift; Delay | `ifx_pitch` | not started |
| Hush; Chorus; Delay; Reverb | `ifx_chorus_reverb` | not started |
| Hush; Pitch Shift; Delay; Reverb | `ifx_pitch_reverb` | not started |

## What a plugin will do

- Run the library at the device's own sample rate (about 39 kHz, a fixed constant) and resample
  to and from the host's rate; the library is bit-exact only there.
- Feed it one mono input, as the device's single A/D does, and take its stereo output.
- For the device's analog Direct Pre path, pass the stereo dry signal through the library's Direct
  Level helper; Direct Post is part of each effect.

## Disclaimer

This is not a clean-room reimplementation: bit-exactness needs the firmware's tables and arithmetic
translated verbatim. Depending on where you live, that may be considered copyright infringement.
The repository holds neither the firmware nor the manual.

## Make targets

| Target | What it does |
|---|---|
| `make ghidra-prepare` | verify the EPROM, split it, disassemble both processors and build the Ghidra project |
| `make ghidra-clean` | remove what `ghidra-prepare` generates, keeping the Ghidra project |
| `make static-check` | `ruff check` and `ruff format --check` on the workspace scripts |
| `make clean` | remove `build/` |

Put the EPROM (and optionally the manual) into the project root or `reverse_engineering/inputs/`
under any name; the scripts find them by SHA-256 ([`reverse_engineering/README.md`](reverse_engineering/README.md)).

## Dependencies

- `python`, `ruff`: the workspace scripts
- xemu DSP56300 toolkit ([xemu-project/dsp56300](https://github.com/xemu-project/dsp56300)): `dsp56300-disasm`, later the emulator library
- Ghidra 12.2 with the Ghidra-Rare DSP56300 extension ([aleph-void/ghidra-dsp56300](https://github.com/aleph-void/ghidra-dsp56300)) and PyGhidra

## Layout

| Path | Contents |
|---|---|
| `reverse_engineering/` | the workspace scripts and the firmware map ([`EFFECTS.md`](reverse_engineering/EFFECTS.md)) |
| `AGENTS.md` | the project's rules and decisions |
