# Firmware map and reimplementation notes

The current technical reference for the native library. It records verified findings only; use the
disassembly (`reverse_engineering/asm/`) and, once it exists, the harness for details. EPROM offsets
are `0xNNNN`, MCU addresses `C:NNNN` (code) and `X:NNNN` (XDATA), DSP addresses `P:$NNNN`,
`X:$NNNN` and `Y:$NNNN` (DSP56001 word addresses).

## Current status

No effect is implemented. The workspace splits and disassembles the EPROM and builds the Ghidra
project; the firmware map below is in progress.

## EPROM layout

| EPROM | Content |
|---|---|
| `0x0000..0x4AEF` | 8051 program and data: code, factory preset names from `0x1C54` (80 × 13 bytes, presets 81–160), preset data, UI strings and parameter names about `0x39B0..0x4AE5`, a 10-byte table at `0x4AE6` |
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

The configuration index is the one the MCU's image switch uses (below) and the order of the
16-character configuration-name table at `0x45A4`: both map index 6 to configuration 0 and treat 7
as invalid (`NOT A CONFIG`).

## MCU (8051)

- **Clock.** `TMOD=0x21`, `TH1=0xFF`, `PCON.SMOD=0`, `SCON=0x70` (mode 1, receive on): 31,250 baud
  MIDI, which fixes the crystal at 12 MHz.
- **Vectors.** Reset `C:0000` → `C:0026`; timer 0 `C:000B` (`clr TR0`) → `C:14E3`; serial
  `C:0023` → `C:1A3A`. External interrupts 0 and 1 and timer 1 are unused (`0xFF`). `IE=0x92`
  (EA, ES, ET0), `IP=0x10` (serial high).
- **Timer 0** multiplexes the 14-segment display through `X:8004..8007` (font at `C:1834`) and
  reloads TH0/TL0 with a computed value.
- **Host port.** The firmware reaches the DSP's host interface as `MOVX @R0` with `R0 = 0` and
  `P2 = 0x80 + n`, so register n sits at `X:8n00`: 1 CVR, 2 ISR, 5/6/7 TXH/TXM/TXL. The display's
  `X:8004..8007` and the panel inputs at `X:8002` share the `0x80` page; the board's decode is not
  known.
- **Waits.** `C:11BB` waits for CVR.HC to clear, `C:11D3` for ISR.TRDY and `C:11EA` for ISR.HF2,
  each until a counter at IRAM `0x3E` reaches 100; on timeout it sets `SP=8` and jumps to `C:00C1`.

## DSP load protocol

- **Bootstrap.** `C:0F96` sets P3.4 and writes the 211 bootstrap words as TXH/TXM/TXL triples with
  no handshake: the DSP56001's host bootstrap. `C:0FC2` then sends host command `$2C` (CVR `0x96`)
  and waits for HF2, which the DSP raises once it has run `P:$0069`.
- **Images.** `C:0F66` loads the shared image; `C:0F69` loads the image DPTR points at: it writes
  the load address as one host word, sends host command `$24` (CVR `0x92`), waits for it to be
  taken, then writes the words through the same loop `C:0FA4` as the bootstrap. On the DSP, `$24`
  (`P:$005E`) takes the word as R6 with `M6=$FFFF` and enables the host-receive interrupt, whose fast
  vector `P:$0020` stores each word with `movep x:$ffeb,p:(r6)+`.
- **Image switch.** `C:0F2F` takes IRAM `0x30 & 7` through the AJMP table at `C:0F38`: index 0 and 6
  load `0x7242`, 1 `0x7888`, 2 `0x5F9F`, 3 `0x6F29`, 4 `0x66B1`, 5 `0x6AF6`, 7 goes to `C:0F26`.

## DSP56001 runtime

The vector map and the peripheral addresses are the DSP56001's (a DSP56001FC27 is reported in an
Intellifex). While a configuration runs, the DSP holds the bootstrap's `P:$0000..$009F`, the
configuration at `P:$00A0` (over the bootstrap's reset code) and the shared image at `P:$0400`.
Internal P RAM ends at `P:$01FF`, so most of the program runs from external P memory.

**Reset** (`P:$0097`): `OMR=0` (the on-chip data ROMs stay off, and nothing writes OMR again),
`BCR=$1111`, `HCR=$0004` (host-command interrupts), `PBC=1` (port B is the host interface),
`PCDDR=7`, `PCD=4`, SSI `CRA=$4000`, `CRB=$F110`, `PCC=$01F8`, `IPR=$3800` (host at level 1, SSI at
level 2), interrupt mask lowered to level 1, then the main loop.

**Vectors** (two words each; "fast" executes the two words, "jsr" is a long interrupt):

| Vector | Content |
|---|---|
| `$0C`, `$0E` SSI receive (and exception) | fast: `movep x:$ffef,x:(r0)+`, `move r0,x:$0001` |
| `$10` SSI transmit | fast: `movep y:(r7)+,x:$ffef`, `move r7,x:$0002` |
| `$12` SSI transmit exception | jsr `$0014`: clears the status, then transmits as `$10` |
| `$20` host receive | fast: `movep x:$ffeb,p:(r6)+` |
| `$24` host command, CVR `0x92` | jsr `$005E`: load address into R6, host-receive interrupt on |
| `$26` / `$28`, CVR `0x93` / `0x94` | fast: set / clear bit 5 of `X:$0000` |
| `$2A`, CVR `0x95` | jsr `$0480` (shared image) |
| `$2C`, CVR `0x96` | fast: set bit 2 of `X:$0000` |
| `$2E`, CVR `0x97` | jsr `$0490` (shared image) |
| `$30`, CVR `0x98` | jsr `$0075`: set bits 3 and 10 of `X:$0000`, clear HF2 |
| `$32`, CVR `0x99` | jsr `$0063`: host-receive interrupt off; with bit 8 set, `X:$0010 = R6` |

**Main loop** (`P:$0038`): with bit 2 of `X:$0000` set it runs `P:$0069` (sets bit 11, raises
HF2); with bit 11 set `P:$0079`, which clears `Y:$0F40..$0F7F` and then `Y:$1000..$FFFF` in
2048-word steps from the top; with bit 10 set it jumps to the shared image's `P:$0400`.

**Audio interface.** The SSI uses 16-bit words. Receive clock and frame sync are external (SC0,
SC1); the transmit clock is external (SCK) and the DSP drives the transmit frame sync (SC2). Received
words go into a 64-word ring at `X:$0080` (R0, `M0=$3F`); transmitted words come from a 128-word
ring at `Y:$0080` (R7, `M7=$7F`). The shared code works through the input in blocks of 32 samples
(the bootstrap's loops at `P:$0040`, `P:$004A`, `P:$0050`) and, whenever the host transmitter is
empty, sends the MCU a level word through `HTX` (`P:$0410`).

## Sample rate

The firmware never sets the rate: the SSI's clocks are external. Evidence so far:

- The handler at `P:$074B` stores `78 × (v & $FF)` into `Y:($0EC0 + 8·voice + 3)` for voices 0–7.
  If v is the 2 ms step of the manual's 0–418 ms voice delays, a millisecond is 39 samples and the
  rate 39 kHz. The MCU side that sends v is not yet traced.
- The manual's wet response is −3 dB at 18 kHz (page 57), which a 39 kHz rate allows.

The exact rate depends on the codec's clock (not yet known).
