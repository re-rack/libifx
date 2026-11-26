# How the tests work

The library is checked against the original firmware, not against expectations
written by hand. The Intellifex LTD's firmware runs in the two-CPU harness in
`reverse_engineering/harness/`, the recorder stores what it computes, and
`make check` requires the library to reproduce it exactly: every table word and,
once effects exist, every output sample. A difference of one LSB fails.

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
  sweep, clicks and a 110 to 300 Hz note change, mono at 39 kHz), never stored.
  The fixtures hold only the firmware's answers.

## Fixtures

A fixture (`ifx_fixture.h`) is the magic `IFXFIX01` and little-endian sections of
records, each section a kind, a record size and a record count. The recorder
writes `<path>.tmp` and renames it, so a failed run leaves the old file alone.

`tables.bin` holds a table index (each table's name, first word and word count)
and the table words: the ten shared-image tables at `P:$07BE..$0A0F` as the DSP's
P memory holds them after boot, and the MCU's 108-byte parameter window table at
EPROM `0x12C8`. It proves that `src/ifx_tables.c` is the firmware's, word for word.

## How the checker compares

- **Tables:** every table's word count and words against the fixture, which must
  hold every table the library has and no other.
- **Host:** what has no firmware counterpart, checked against its definition.
  `ifx_context_init` accepts a finite positive rate and refuses NULL, zero,
  negative, NaN and infinite rates. `IFX_PROCESS_DEFINE` runs a test-only block
  function: the first 32 output frames are the zero rest output, output frame m
  is the block's answer for input frame m − 32, a block uses the config passed
  with the frame that completes it, all four entry points agree over random
  buffer sizes (0, 1, 31, 32, 33 and large among them), split and float work in
  place, and junk in the upper eight input bits is ignored.
- **Converter:** `make check-convert` checks `ifx_convert.c` on every float and
  every Q23 pattern, on each SIMD path the machine has and on NEON and RVV under
  qemu; `make check` and `make sanitize` run its quick subset.
- **Packaging:** `make check-install` installs the library into a staging
  directory and builds a C and a C++ host from `ifx.h` and `libifx.a` alone.

## Limits

- No effect exists yet, so there is no audio fixture and no effect replay.
- The tables fixture covers the shared image; a configuration's own tables come
  with its effect.
- Everything is at the device rate; no arithmetic depends on the rate.
