# Firmware map and reimplementation notes

The current technical reference for the native library. It records verified findings only; use the
disassembly (`reverse_engineering/asm/`) and, once it exists, the harness for details. EPROM offsets
are `0xNNNN`, MCU addresses `C:NNNN` (code), `X:NNNN` (XDATA) and `IRAM xx`, DSP addresses
`P:$NNNN`, `X:$NNNN` and `Y:$NNNN` (DSP56001 word addresses). "Open" marks what static reading
has not settled; the harness settles it.

## Current status

Configuration 3 (`ifx_delay`, Hush; Delay; Ducker) is implemented with its fixture. The workspace
splits and disassembles the EPROM and builds the Ghidra project, and the harness runs the original
firmware on both CPUs. The MCU's parameter model, the MCU–DSP protocol, the DSP's shared code and
the structure of every configuration are mapped; the other five configurations' kernels are still
to be settled in the harness.

## Hardware

A teardown of a 1997 Replifex, which Rocktron staff describe as the LTD's board, shows a Siemens
SAB 80C32 (an 8052: 256 bytes of IRAM, timer 2), a Motorola DSP56001AFC33, external SRAMs, a
Motorola DSP56ADC16S sigma-delta A/D (64× oversampling, 16 bits, output rate CLKIN/128, serial
clock CLKIN/4, 32 serial clocks per frame), a Philips TDA1541A dual 16-bit D/A, an Ecliptek
oscillator marked "?3.000M" beside a 74AC161 counter, and an AT28C256 EEPROM. The firmware agrees:
one 16-bit mono input word and two output words per sample period.

## EPROM layout

| EPROM | Content |
|---|---|
| `0x0000..0x4AEF` | 8051 program and data: code, factory preset names (`0x1C54`, 80 × 13 bytes), factory presets (`0x2084`), parameter tables, UI strings and parameter names about `0x39B0..0x4AE5` |
| `0x4AF0..0x7EEE` | Eight DSP images, packed end to end |
| `0x7EEF..0x7FFF` | `0xFF` fill |

The DSP bootstrap image has a 2-byte big-endian word count; every other image a 2-byte big-endian
load address and a 2-byte word count. Words are 24-bit big-endian.

| EPROM | Loads at | Words | Content |
|---|---|---|---|
| `0x4AF0` | `P:$0000` | 211 | bootstrap: vectors, helpers, reset code |
| `0x4D6B` | `P:$0400` | 1552 | shared code and tables |
| `0x5F9F` | `P:$00A0` | 602 | configuration 2, Hush; Reverb |
| `0x66B1` | `P:$00A0` | 363 | configuration 4, Hush; 8 Voice Chorus; Delay |
| `0x6AF6` | `P:$00A0` | 357 | configuration 5, Hush; Pitch Shift; Delay |
| `0x6F29` | `P:$00A0` | 263 | configuration 3, Hush; Delay; Ducker |
| `0x7242` | `P:$00A0` | 534 | configuration 0, Hush; Chorus; Delay; Reverb |
| `0x7888` | `P:$00A0` | 545 | configuration 1, Hush; Pitch Shift; Delay; Reverb |

The configuration index is the one the MCU's image switch uses and the order of the configuration
name table at `0x45A0` (16 characters each): both map index 6 to configuration 0 and treat 7 as
invalid (`NOT A CONFIG`).

## MCU (80C32)

- **Clock.** `TMOD=0x21`, `TH1=0xFF`, `PCON.SMOD=0`, `SCON=0x70`: 31,250 baud MIDI, so a 12 MHz
  crystal.
- **Vectors.** Reset `C:0000` → `C:0026`; timer 0 `C:000B` (`clr TR0`) → `C:14E3`; serial
  `C:0023` → `C:1A3A`; the others are unused. `IE=0x92`, `IP=0x10`.
- **Timer 0** multiplexes the display (`X:8004..8007`, font at `C:1834`) with a reload of
  `0xFC00 + 32·(15 − digit)`, increments the timeout counter IRAM `3E`, reads the knobs, and every
  32 ticks reads the DSP's level word for the input meter.
- **Host port.** `MOVX @R0` with `R0 = 0` and `P2 = 0x80 + n` reaches DSP host register n at
  `X:8n00`: 1 CVR, 2 ISR, 5/6/7 TXH/TXM/TXL (RXH/RXM/RXL when read). Panel inputs (`X:8002`), the
  display and the meter LEDs (`X:8003`) share the `0x80` page; the board's decode is not known.
- **Waits.** `C:11BB` waits for CVR.HC to clear, `C:11D3` for ISR.TRDY, `C:11EA` for ISR bit 3,
  each until IRAM `3E` reaches 100; on timeout `C:00C1` shows `RESETTING DSP` and reboots the DSP.

### Parameters

- **Store.** Parameter i lives at IRAM `0x80 + i` (i < 108). A preset's parameters are 55 bytes;
  user presets sit in battery RAM at `X:0A81 + 55·p`, names at `X:1BEF + 13·p`, controller maps at
  `X:0081 + 32·p`.
- **Windows.** The value runs from 0 to `max − 1`, max from the 108-byte table at `0x12C8`
  (`C:12BF`). The knob handler (`C:16B1`, in the timer-0 interrupt) steps ±1 and refuses to leave
  the window.
- **Pages.** `0x368F + 18·config` lists the FUNCTION SELECT pages; `0x37AF + 15·page` lists a
  page's parameters (from the third byte); `0x4070 + 16·page` the page titles; `0x320F + 64·config`
  the configuration's parameter list in preset order (41, 34, 18, 23, 49, 29 entries for configurations
  0–5); `0x338F + 128·config` maps a parameter to its preset offset.
- **Display.** `C:0905` (108 AJMP entries, by parameter) formats the value: dB tables `0x487D`
  (59 entries, −∞ to +6 dB), `0x4969` (59, −∞ to 0 dB), `0x4A73` (25, −92 to −20 dB), Hush release
  `0x4A55` (25 to 800 ms), Ducker release `0x4ABE` (0.2 to 9.0 s), pitch `(v − 120)·20` cents, fine
  `v − 20` cents, voice delay `2·v` ms, Delay times `5·v` ms (`10·v` for 2-Tap).
- **Send.** `C:0EE0` sends one parameter as two host words, `0x0000ii` (the parameter index) and
  `0x0000vv` (the value), written as TXL twice with TXH/TXM left at 0, then CVR `0x95` (host
  command `$2A`). The knob, COMPARE (`C:066C`, `C:0679`) and MIDI controllers use it.
- **Direct levels.** L DIR LVL (3) and R DIR LVL (4) also drive latches `X:8101` and `X:8001`
  through the 59-entry curve `0x31CE`, while DIRECT HUSH (5) is PRE; POST zeroes the latches. They
  are the analog direct path's levels.
- **MIDI controllers** (`C:0759`): `LLIM + round((128 − cc)·(ULIM − LLIM)/128)`.

### Presets

A preset is 55 bytes: byte 0 is the configuration, then the values in the configuration's list
order (`0x320F + 64·config`), zero-padded. Factory records carry `0x08 | config` in byte 0, which
the restore (`C:10F0`) masks to 0..7; recall (`C:0DED`) reads a byte of 6 or more as
configuration 0. Factory preset p (81–160) is at `0x2084 + 55·(p − 81)`; preset
83 STADIUM decodes to configuration 2 with REV TYPE 6 (Stadium). Factory presets per configuration:
0: 16 (with preset 160 BYPASS, index 6), 1: 16, 2: 14, 3: 6, 4: 12, 5: 16.

## DSP load protocol

- **Boot** (`C:00C7`): `C:0F94` pulses P3.4 and writes the 211 bootstrap words as TXH/TXM/TXL
  triples with no handshake (the DSP56001's host bootstrap); then the shared image (`C:0F66`),
  host command `$32`, host command `$2C` with the wait for ISR bit 3 (`C:0FC2`), the bulk parameter
  send (`C:0D6A`), `$32`, the configuration image (`C:0F27`), `$32`, the direct-level latches, and
  host command `$30`.
- **Images.** `C:0F69` writes the load address as one host word, sends host command `$24` (CVR
  `0x92`), then the words through `C:0FA4`. On the DSP, `$24` (`P:$005E`) takes the word as R6 with
  `M6=$FFFF` and enables the host-receive interrupt, whose fast vector `P:$0020` stores each word
  with `movep x:$ffeb,p:(r6)+`. `C:0F2F` picks the image by IRAM `0x30 & 7` through the AJMP table
  `C:0F38`, only when bit 7 of `0x30` says the configuration changed.
- **Bulk parameters** (`C:0D6A`): host command `$2E` (CVR `0x97`), then per parameter of the
  configuration's list the pair (index, value) from the preset, with no handshake; `$32` ends it.
  The DSP queues the words in its message ring and runs them once processing starts.
- **Preset recall** (`C:0384`): `$2C`, bulk parameters, `$32`, the configuration image if it
  changed, `$32`, latches, `$30`. No reboot.
- **Factory restore** lives on preset 160's page list: its ENTER CODE parameter set to 249, then
  STORE. It writes the battery tables and shows `ERRORS 0`.

## DSP56001 runtime

The vector map and peripheral addresses are the DSP56001's. A running configuration has the
bootstrap's `P:$0000..$009F`, the configuration at `P:$00A0` and the shared image at `P:$0400`.

**Memory.** Internal: P `$0000..$01FF`, X and Y `$0000..$00FF`, separate. External memory is one
array that P, X and Y all address: the bulk path stores message words as P and the dispatcher reads
them as Y (`$0F80..$0FFF`); the configurations write their output as Y and the mixer reads it as X
(`$0F40..$0F7F`); configuration 2 walks its Y delay buffer with X moves. The harness must alias them.

| Range | Content |
|---|---|
| `X:$0000` | flag word (below) |
| `X:$0001`, `X:$0002` | SSI receive and transmit pointers, stored by the fast vectors |
| `X:$0003..$0028` | block pointers, meter peak, Hush state, mixer levels |
| `X:$0080..$00BF` | input ring, 64 words (R0, `M0=$3F`) |
| `Y:$0000..$002B` | configuration parameter words |
| `Y:$0080..$00FF` | output ring, 128 words (R7, `M7=$7F`), L and R interleaved |
| `$0EC0..$0EFF` | eight voice blocks of eight words |
| `$0F00..$0F1F` | the block after Hush, which the configurations read |
| `$0F40..$0F7F` | the configuration's stereo output block |
| `$0F80..$0FFF` | message ring, outside the memory wipe |
| `$1000..$FFFF` | delay memory, wiped at start (`P:$0079`) |

**Reset** (`P:$0097`): `OMR=0` (the data ROMs stay off; nothing writes OMR again), `BCR=$1111`,
`HCR=$0004`, `PBC=1`, `PCDDR=7`, `PCD=4`, SSI `CRA=$4000` (16-bit words), `CRB=$F110`,
`PCC=$01F8`, `IPR=$3800` (host at level 1, SSI at level 2), mask lowered to level 1. SSI receive
clock and frame sync and the transmit clock are external; the DSP drives the transmit frame sync.

**Vectors** (fast: the two words run in place; jsr: a long interrupt):

| Vector | Content |
|---|---|
| `$0C`, `$0E` SSI receive (and exception) | fast: `movep x:$ffef,x:(r0)+`, `move r0,x:$0001` |
| `$10` SSI transmit | fast: `movep y:(r7)+,x:$ffef`, `move r7,x:$0002` |
| `$12` SSI transmit exception | jsr `$0014`: clears the status, then transmits as `$10` |
| `$20` host receive | fast: `movep x:$ffeb,p:(r6)+` |
| `$24`, CVR `0x92` | jsr `$005E`: load address into R6, host receive on |
| `$26` / `$28`, CVR `0x93` / `0x94` | fast: set / clear bit 5 (mute the effect) |
| `$2A`, CVR `0x95` | jsr `$0480`: queue two host words, set bit 1 |
| `$2C`, CVR `0x96` | fast: set bit 2 (the main loop restarts) |
| `$2E`, CVR `0x97` | jsr `$0490`: bulk mode, host receive into the message ring |
| `$30`, CVR `0x98` | jsr `$0075`: set bits 3 and 10 (initialise and run) |
| `$32`, CVR `0x99` | jsr `$0063`: host receive off; after bulk mode, `X:$0010 = R6` |

**Flag word `X:$0000`:** bit 0 block parity, 1 messages pending, 2 restart request, 3 the
configuration initialises on its next call, 4 Hush in, 5 effect muted, 6 direct on, 7 wipe pointer
valid, 8 bulk mode, 9 reverb gate on, 10 running, 11 memory wipe running (the configuration is not
called; `P:$008A` clears it when the wipe ends), 12 skip one call,
16 set by messages `$69`/`$6A` and read nowhere, 17 read by configurations 0 and 1.

**Main loop** (`P:$0038`): bit 2 runs `P:$0069` (sets bit 11, raises ISR bit 3 for the MCU); bit
11 runs the wipe `P:$0079`, which clears `Y:$0F40..$0F7F` and then `Y:$FFFF` down to `Y:$1000` in
2048-word steps, one step per pass; bit 10 jumps to `P:$0400`.

### The block (`P:$0400`)

One pass per 32 input samples: the tail waits for the receive pointer and the transmit pointer to
cross their rings' halves, so each pass reads 32 input words and writes 64 output words.

1. Messages (`P:$04A0`, below) when bit 1 is set.
2. **DC blocker and level** (`P:$0040`, 32×): `lp = 0.99900·lp + 0.0010001·x` (`$7FDF3B`,
   `$0020C5`, state `X:$000D`); the input ring is overwritten with `lp − x`; the block level is
   the largest magnitude of `lp − x` (`X:$0018`), and the meter peak `X:$0003` its maximum since
   the last host send (`P:$0410`).
3. **Hush gain** (`P:$0417..$043B`): target 1.0 at or above the threshold `X:$0006`, else
   `level·X:$0007·2^16` (the threshold and slope tables keep it continuous); a smoother on
   `X:$0008` with release coefficient `X:$0009`; gain `2g − g²` with `g = 1.1·X:$0008`, or
   `$7FFFF8` with Hush out (bit 4 clear); the block's multiplier is `0.125·gain`.
4. The 32 samples times that multiplier go to `$0F00..$0F1F` (`P:$004A`).
5. The configuration (`jsclr #11,x:$0000,$00a0`), which writes `$0F40..$0F7F`.
6. **Mixer** (`P:$0050`, 32×): `L = 8·(wet·C_L + dirL·H) + 0.05`, `R = 8·(dirR·H + wet·C_R) +
   0.05`, limited on the store, with C the configuration's pair, H the Hush block, wet `X:$0013`
   (0 while bit 5 is set) and dirL/dirR `X:$0014`/`X:$0015` (0 while bit 6 is clear). The 0.05 is
   a constant DC offset on both outputs: silence comes out as `$066666` in the harness.

Output trails input by 64 frames, the receive and transmit rings' halves; a parameter message
takes effect at the start of the next block.

### Messages

The dispatcher `P:$04A0` reads (index, value) pairs from the message ring (`X:$000F` read,
`X:$0010` write, both `$0F80` at reset, `M=$7F`) and jumps through the 128-entry table
`P:$04BD` by `index & $7F`. The index is the MCU's parameter index. Handlers store a table entry,
a product or a flag:

| Table | Entries | Content |
|---|---|---|
| `P:$07BE` T1 | 59 | level: 0, then −48, −42, −38.5, −36 … in steps down to 0.5 dB, last 0 dB |
| `P:$07F9` T2 | 101 | `cos(πk/200)`; the right gain reads `P:$085D − k` |
| `P:$085E` | 25 | threshold, 2.5e-5 to 0.1 |
| `P:$0877` | 25 | the matching slope |
| `P:$0890` | 10 | Hush release coefficient |
| `P:$089A` | 20 | Ducker release coefficient |
| `P:$08AE` | 32 | gate decay per block |
| `P:$08CE` | 100 | gate hold, in blocks |
| `P:$0932` | 181 | `2^(k/60 − 4)`: pitch in 20-cent steps |
| `P:$09E7` | 41 | `0.5·2^((k − 20)/1200)`: fine pitch in cents |

Selected handlers: 2–4 effect, L and R direct level (`X:$0013..$0015` from T1); 5 DIRECT HUSH
(bit 6: POST mixes the Hush block in as the direct signal, PRE leaves the direct path analog); 9
Hush in (bit 4); 10 threshold and slope; 11 release; voice handlers write the voice block
`$0EC0 + 8·v`: +0 rate `(v & $FF)·$0020C5` or coarse pitch (T `$0932`), +1 depth
`(v & $7F)·$0020C5` or fine pitch (T `$09E7`), +2 their product, +3 delay `78·(v & $FF)` samples,
+4 level (T1), +5 pan 0–100, +6 and +7 level·T2 for left and right; 97/98 Regen L/R
(`Y:$000F`/`Y:$0010` from T1); 42 REVERB DECAY `Y:$0000 = $4CCCCD + (v & $7F)·$004189` and 43
RV HF DAMP `Y:$0001 = (v & $7F)·$004189` (low words of the products), each then restaging
`Y:$0002 = Y:$0000·(1 − Y:$0001)`; 59 gate on (bit 9); 35 sets one of `Y:$0018` bits 3–5 for
values 0–2, and 44 and 55 store `v & 7` in `Y:$000A`: unless bit 3 is already set, these three
restart the memory wipe (`X:$0017 = $FFFF`, bits 7 and 11) and reinitialise the configuration
(bit 3). The full table is in `dsp_shared.asm`
(`P:$053D..$07BD`).

## Configurations

Every configuration runs from `P:$00A0` once per block: with bit 3 set it initialises (tables
copied from its image, pointers, state), otherwise it processes the Hush block `$0F00..$0F1F` into
`$0F40..$0F7F`. None calls the shared image. Delay lines advance 32 words per block.

| Configuration | Structure | Delay memory |
|---|---|---|
| 0 Chorus/Delay/Reverb | regen limiter, regen sum, 16-tap lowpass, 4 voices (32-entry LFO table, interpolated reads), L/R delay lines, ducker, reverb network (14 pointer/coefficient pairs), mixer | L/R `$4000`, `$8000` (16,320 words); reverb `$C000`; history `$2000` |
| 1 Pitch/Delay/Reverb | configuration 0's frame and reverb with 2 pitch voices and Delay 3 | as configuration 0 |
| 2 Reverb | per-type 48-word state tables (8 types), predelay `39·v` samples, gate | `$7FBB..$FF76` (32,763 words) |
| 3 Delay/Ducker | regen gains `Y:$0024`/`$0025`, two 29,632-word lines, ducker | `$73BF` modulo windows |
| 4 8 Voice Chorus | configuration 0's chorus core with 8 voices | L/R rings `$4000`, `$8000` (16,384) |
| 5 Pitch Shift/Delay | configuration 1's pitch core with 4 voices, two grains each | rings `$8000`, `$C000`; grains `$2000` |

- **Shared cores** (identical opcode runs): chorus in configurations 0 and 4, pitch in 1 and 5,
  reverb and mixer in 0 and 1, the ducker in 0, 1 and 3.
- **Regeneration limiter.** Configurations 0, 1, 4 and 5 sum the voices' pan gains times the
  regen gains each block (`cfg4 P:$00DC`), configuration 3 the two regen words, in 2-Tap only
  (`cfg3 P:$00A8`); while the sum reaches 1.0 they scale both regen words by 0.8889 (`$71EB85`)
  and store them back. A Regen turn rewrites only its own word, so the other stays scaled. This is
  the manual's Regeneration Limiting, which it dates to software 1.01.
- **Ducker** (`cfg0 P:$01B4`): towards the attenuation word `Y:$0027` with coefficient `$76B296`
  while the input is loud, towards 1.0 with `Y:$0028` otherwise.
- **Configuration 3 delay** (`cfg3 P:$00A0..$01A6`): the front, Hush and mixer are shared. The
  configuration keeps two 29,632-word lines at base `$1800` and `$8C00`, with write pointers
  `Y:$0015`/`$0016`, damping states `Y:$002C`/`$002D`, and reads each line through its delay word.
  Stereo (`D TYPE` 0, `Y:$0018` bit 3) writes each line from its input plus its own regen;
  Ping-Pong (1, bit 4) writes line 1 from input plus regen 2 times line 2 and line 2 from regen 1
  times line 1; 2-Tap (2, bit 5) uses one interleaved pair line at `$1800`, pointer in pairs, and
  sums input plus both regens into it. The 2-Tap path runs the shared regen limiter every block.
  The write filter is `out = in + 2·damp·state − 2·damp·in`, with `damp` from
  `Y:$0019 = low(2·v·$00902E)`. Each tap word is read through `mpyr` with `$7FFFFF`, or 0 while
  DELAY is off with MUTE TYPE muting the output, which takes one LSB off taps of 2^22 and more.
  2-Tap reads 16 pairs, 32 consecutive words. A delay word is `196·v` samples (pairs in 2-Tap),
  `v = 0` reading as 1.
- **Configuration 3 Ducker**: `Y:$0029 = target + 2·coef·(current − target)`, target `Y:$0027`
  and coefficient `$76B296` while `|level| >= |Y:$0026|`, else target 1.0 and coefficient
  `Y:$0028`.
- **Configuration 3 wipe**: message 35 sets the mode bits at once; only if `X:$0000` bit 3 is
  clear does it set `X:$0017 = $FFFF` and bits 7, 11 and 3. The arriving pass skips the
  configuration (`P:$0447`) and reuses the old `$0F40..$0F7F`, the repeated block. Each later pass
  clears `$0F40..$0F7F`, then while `X:$0017 > $1000` clears 2048 words ending at the pointer and
  subtracts `$800` (`$FFFF → $F7FF → … → $17FF → $0FFF`). The next call clears bit 11 and sets
  bits 1 and 12; that pass still skips the configuration and clears bit 12, and the following pass
  runs the init (`P:$00A2`: clears bit 3, zeroes `Y:$0015`, `Y:$0016`, `Y:$002C`, `Y:$002D`) and
  processes the block with the current mode. The ducker, being part of the configuration, holds
  still meanwhile. A mode turn while bit 3 is set changes the mode bits but does not restart or
  extend the wipe.
- **Configuration 3 defaults**: factory preset 84 (`SPARE CHANGE`) at EPROM `0x2084 + 55·3`;
  the 23 values are DELAY LVL 48, L/R DIR 46, DIRECT HUSH PRE, HUSH IN, EXP THRESH 8, REL RATE 5,
  DELAY active, MUTE TYPE PRE, LEVEL 1/2 58, PAN 1 0, PAN 2 100, TIME 1 75, TIME 2 50, REGEN 1 46,
  REGEN 2 52, D TYPE Stereo, DL HF DAMP 12, DUCKER on, SENSITIVITY 20, ATTENUATION 19, RELEASE 8.

## Sample rate

The firmware never sets the rate: every clock is external. A millisecond is 39 samples wherever
the firmware converts one: voice delays `78·v` per 2 ms step, predelay `39·v`, and the buffers
fit their manual maxima at that rate (16,320 words for 418 ms, 8,192 for 209 ms, 2 × 29,632 for
1500 ms). In the harness, preset 84's echoes fall 196 samples apart per 5 ms DLY TIME step. The
A/D's rate is CLKIN/128; 5 MHz would give 39,062.5 Hz. The oscillator's marking does not settle
CLKIN.

## Open

- The scale conventions: configuration tables hold words such as `$008000` that read like Q1.16,
  next to Q1.23 constants elsewhere.
- Modulo addressing with `N = $2000` on `M = $1FFF` (configurations 4 and 5): the DSP56000's
  multiple wrap-around rule must hold in the harness.
- Configuration 4's LFO rate scaling and pointer sweep, configuration 5's grain window source
  (`Y:$1C80`) and net pitch factor, the reverb network's topology.
