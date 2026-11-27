# How the tests work

The library is checked against the original firmware, not against expectations
written by hand. The Intellifex LTD's firmware runs in the two-CPU harness in
`reverse_engineering/harness/`, the recorder stores what it computes, and
`make check` requires the library to reproduce it exactly: every table word and
every output sample. A difference of one LSB fails.

## Record once, check offline

```
firmware --harness--> ifx_record --> tests/fixtures/<name>.bin
                                             |
           library --> ifx_check <-----------+
```

- `ifx_record` needs the EPROM and links the harness; `ifx_check` needs neither,
  so anyone can verify the library from a plain clone.
- Recording is deterministic. A fixture changes only when what it describes
  changes (the tables, an effect's knob table or script, the input signal, the
  fixture format), never because `src/` changed.
- Inputs are generated (`ifx_signal.c`: noise, held notes, bursts, quiet noise, a
  sweep, clicks, a 110 to 300 Hz note change and a long silent stretch, mono at
  39 kHz), never stored. The fixtures hold only the firmware's answers.

## Fixtures

A fixture (`ifx_fixture.h`) is the magic `IFXFIX01` and little-endian sections of
records, each section a kind, a record size and a record count. The recorder
writes `<path>.tmp` and renames it, so a failed run leaves the old file alone.

| Kind | Records |
|---|---|
| `IFX_FIX_KNOBS` | the effect's knob table: name, minimum, maximum and default |
| `IFX_FIX_SETUP` | one knob vector and the config words the firmware staged |
| `IFX_FIX_SEGMENT` | one knob vector and the frames it runs for |
| `IFX_FIX_OUTPUT` | one device output frame, 24-bit L and R words |
| `IFX_FIX_TABLE_INDEX` / `_WORDS` | a lookup table's name, first word and words |

`tables.bin` holds the library's eleven tables as the DSP's P memory holds them
(the ten shared-image tables at `P:$07BE..$0A0F` and configuration 2's reverb type
table at `P:$0172`), proving `src/ifx_tables.c` is the firmware's word for word,
and the MCU's 108-byte window table at EPROM `0x12C8`, each parameter's exclusive
maximum.

An effect fixture holds its knob table, setup records for each knob on its own
(every position when it has 32 or fewer, else 32 spread positions plus the
default), four multi-knob presets (every knob at 33 %, 66 %, minimum and
maximum, built one knob at a time), the configuration's factory presets, and one
audio run with every knob swept and given fast turns 64 frames apart.

Each knob also carries an explicit active value, chosen so all of the effect's
paths move: for Delay that is DELAY on, DUCKER on, HUSH on with a moving
threshold, DIRECT HUSH at POST, short delay times, and high regeneration. A knob
marked as a mode knob gets one full active walk per value; D TYPE therefore runs
the whole walk for Stereo, Ping-Pong and 2-Tap. Each walk starts with the mode
change and an 8,192-frame settle, long enough for the wipe and several echoes.
Delay-time knobs hold their maximum long enough for the buffer to wrap in every
mode. The audio run starts through `ifx_emu_start` at the first frame of the
block whose pass initialises the configuration; fixture output frame m is the
device's output at feed index m + 32.

The signal's four-pattern cycle ends with cycle 3 silent for its first 2048
frames, so the impulse at the end of cycle 2 runs into a quiet stretch of 80
blocks, long enough for the reverb gate's shortest hold. A per-effect list of
extra scripted turns (`ifx_effect_extra`) runs after the generated segments:
`tests/effect_reverb.c` uses it to raise RV HF DAMP while the gate holds a gain
staged with REVERB DECAY 99 and no damping, `tests/effect_delay.c` to mute the
delay under each MUTE TYPE while the lines hold signal, and the chorus and pitch
effects to walk the voice sweeps into the phase re-sync and to set levels, pans
and regeneration whose limiter sum lands on its rounding boundary.

## How the checker compares

- **Tables:** every table's word count and words against the fixture, which must
  hold every table the library has and no other. The fixture's window table then
  ties each knob to the firmware: its `min` must be 0 and its `max + 1` the
  window entry for its MCU parameter.
- **Host:** what has no firmware counterpart, checked against its definition.
  `ifx_context_init` accepts a finite positive rate and refuses NULL, zero,
  negative, NaN and infinite rates. `IFX_PROCESS_DEFINE` runs a test-only block
  function: the first 32 output frames are the zero rest output, output frame m
  is the block's answer for input frame m − 32, a block uses the config passed
  with the frame that completes it, all four entry points agree over random
  buffer sizes (0, 1, 31, 32, 33 and large among them), split and float work in
  place, and junk in the upper eight input bits is ignored.
- **Effect fixtures:** the knob table matches; every setup record's words match;
  setup at min−1 and max+1 equals setup at min and max; configs are filled with
  junk before setup; setup rejects NULL pointers and a context without a rate.
  The audio run replays through split, float, interleaved and frame with random
  buffer sizes and junk in the upper input bits; the output must equal the
  fixture's words minus `0x066666`, saturated to Q23.
- **Converter:** `make check-convert` checks `ifx_convert.c` on every float and
  every Q23 pattern, on each SIMD path the machine has and on NEON and RVV under
  qemu; `make check` and `make sanitize` run its quick subset.
- **Packaging:** `make check-install` installs the library into a staging
  directory and builds a C and a C++ host from `ifx.h` and `libifx.a` alone.

## Targets

```
make check                          every table, host and effect fixture
make check-tables                   the tables fixture
make check-host                     the context and the process entry points
make check-fixture FIXTURE=delay    one effect fixture
make record-fixture FIXTURE=delay   rewrite it from the firmware harness
make record-all-fixtures            rewrite every fixture
make benchmark-all-fx               every effect, ns per frame
make benchmark-fx FX=delay          one effect
```

## Adding an effect

Describe it once: a knob table with each knob's MCU parameter, active value and
default, and its native adapter in `tests/effect_<name>.c`; the configuration
number and its setup record's DSP words in `tests/firmware_<name>.c`; and one
line in `tests/ifx_effects.h`. Then run `make record-fixture FIXTURE=<name>`,
`make check` and `make coverage`, and prove the fixture by mutation: a constant
off by one LSB, a rounding dropped or a branch changed in each new source must
make `make check` fail.

## Limits

- Each fixture is one scripted run; behaviour the script never reaches is not
  covered.
- Internal state is not compared, only setup words and outputs.
- Knob turns are recorded through the MCU's send routine, one message per change;
  the panel's one message per detent is checked by the harness, not here.
- Everything is at the device rate; no arithmetic depends on the rate.
