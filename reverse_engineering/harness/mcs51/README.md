# mcs51 emulator core

Vendored copy of [`martinribelotta/mcs51_emulator`](https://github.com/martinribelotta/mcs51_emulator)
`lib/`, commit `8789c25ba7c2e73ab945e6eb6df2aa1e3ca63ab2`, MIT (see `LICENCE.md`). It runs the
Intellifex LTD's SAB 80C32 firmware; the board around it is `../ifx_mcu_board.c`.

## What was changed

Only to make the core compile clean under the project's strict warning set. No behaviour, no
reformatting, nothing renamed except one shadowed local in `uart.c`.

- `cpu.h`: declare the `emulator_{code,xdata}_{read,write}` weak defaults (an embedder overrides
  them; without a declaration `-Wmissing-prototypes` fires).
- `cpu.c`: `__attribute__((unused))` on two dead `static inline` helpers; the `optimize("O3")`
  attribute is GCC-only (clang knows no such attribute); explicit narrowing casts in the RLC and
  ADDC stalls; the `mem_user` cast goes through `uintptr_t` so `-Wcast-qual` stays quiet.
- `decoder.c`, `instr_impl.c`, `timers.c`: `case` labels for the enum values the switches skip, so
  `-Wswitch-enum` is satisfied. Each one falls through exactly where the old `default:` went.
- `uart.c`: narrowing cast on the computed baud, and `rx_scon` for the `scon` local that shadowed
  the one in `uart_update_baud`'s caller.

## Behaviour fixes

Found by running the Intellifex firmware side by side with `s51` (ucsim 0.9.9) from reset, one
instruction per step, XDATA as plain RAM in both. These change behaviour; they are not the
warning-only edits above.

- `instr_list.h`: the cycle column was wrong for 65 of the 256 opcodes (mostly `MOVX`, `MOVC`,
  `CJNE`, `DJNZ`, the bit branches and `LCALL`/`RET`). Replaced with the machine-cycle counts
  `s51` reports, which is what `k_opcode_cycles[]` feeds into `cpu_step`.
- `cpu.c` `MN_MOVX_A_AT_RI` / `MN_MOVX_AT_RI_A` (0xE2/0xE3, 0xF2/0xF3): the address was the 8-bit
  `@Ri` value alone, so every `MOVX` in the firmware's `P2`-banked host port read the wrong place.
  Here it is `(P2 << 8) | Ri`.
- `ports.c` `port_read_effective`: returned `latch & (ext | pull)`, which with the default
  `pull = 0xFF` dropped the external input level entirely and made every input pin read as its
  latch. Here it is `(latch | ~pull) & ext`, so a pin left high reads high and one the firmware drove low
  still reads low.
- `cpu.c` interrupt acknowledge: taking an interrupt cost nothing, so timers ran one machine cycle
  fast per interrupt. Here `cpu_service_interrupt` sets `cpu->vector_cycles`, `cpu_step` adds it to
  the step that vectors, and a harness that polls outside `cpu_step` must add
  `cpu->vector_cycles` to that step and clear it — see `../ifx_mcu_board.c`.

## Cross-check against ucsim

The firmware image (`../../../firmware/00_mcu_0000_19184b.bin`) runs identically in both cores:

| compared | steps | PC/A/B/PSW/SP/DPTR | IRAM dumps | step cycles |
|---|---|---|---|---|
| from reset | 200 000 | 0 mismatches | 3125 dumps, 0 | 0 |
| from reset, with XDATA pokes | 200 000 | 0 mismatches | 3125 dumps, 0 | 0 |
| from reset | 1 000 000 | 0 mismatches | 3907 dumps, 0 | 0 |

## Build

The files are plain C17 with no dependency outside libc; they are part of the harness build. To
compile them on their own:

    cc -std=c17 -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -c *.c

## Tests

The upstream `tests/` pass against this copy, behaviour fixes included: 24 of 24. They are built
as upstream's CMake does: one binary per test, `test_main.c test_common.c <test>.c *.c` with
`-DTEST_ENTRY=<test>` and `-I` pointing here and at `tests/`.

`cpu_poll_interrupts()` is not optional. `cpu_step` polls on its own, but a timer that overflows
between steps, or an SFR write that must take effect immediately, is only serviced by the explicit
call; without it the Intellifex firmware diverges from `s51` at step 827.
