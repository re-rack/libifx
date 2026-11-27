/* The Intellifex LTD's DSP side: the DSP56001 core wired to the 80C32's host
 * port, to the converters on the SSI, and to the port pins.
 *
 * The MCU sees the host interface through ifx_dsp_board_host_*; the board runs
 * the core one instruction at a time and clocks one SSI frame per sample
 * period. Memory sizes, aliasing and peripherals are the vendored core's.
 */
#ifndef IFX_DSP_BOARD_H
#define IFX_DSP_BOARD_H

#include <stdbool.h>
#include <stdint.h>

/* One frame is one input sample, in the core's unit: DSP clock cycles, two per
 * instruction cycle. 33 MHz of DSP clock over 39.0625 kHz. */
#define IFX_DSP_BOARD_FRAME_CYCLES 846u

typedef struct ifx_dsp_board ifx_dsp_board;

/* One board per process: the core keeps its own global state and reports its
 * host interrupt through a plain function pointer. */
ifx_dsp_board *ifx_dsp_board_create(void);
void ifx_dsp_board_destroy(ifx_dsp_board *board);
void ifx_dsp_board_reset(ifx_dsp_board *board);
/* Power-on memory: clears the internal and external words a reset leaves. */
void ifx_dsp_board_clear_memory(ifx_dsp_board *board);

/* MCU side of the host port. `reg` is a CPU_HOST_* register of the core's
 * header; ICR, CVR and the transmit bytes are writable, ISR is read only. */
void ifx_dsp_board_host_write(ifx_dsp_board *board, unsigned reg, uint8_t value);
uint8_t ifx_dsp_board_host_read(const ifx_dsp_board *board, unsigned reg);

/* Run the core: whole instructions until at least `cycles` have elapsed.
 * Nothing runs while the part waits for its bootstrap. */
void ifx_dsp_board_run(ifx_dsp_board *board, uint32_t cycles);

/* A one-shot breakpoint: a step at `pc` stops before executing it. 0xFFFFFFFF
 * clears it. */
void ifx_dsp_board_set_breakpoint(ifx_dsp_board *board, uint32_t pc);
bool ifx_dsp_board_breakpoint_hit(const ifx_dsp_board *board);
void ifx_dsp_board_clear_breakpoint(ifx_dsp_board *board);

/* One SSI frame: hand `in` to the receiver, run a sample period of
 * instructions, then clock out the two words the DAC takes per frame. A frame
 * that found the receiver still full or the transmitter still empty is counted
 * and, for the transmitter, repeats the previous word.
 *
 * `in` is the receive register's word as the A/D's converter fills it; `left`
 * and `right` are the DSP's own 24-bit transmit words, taken before the SSI
 * shifts them to its word length, and signed. */
void ifx_dsp_board_frame(ifx_dsp_board *board, uint32_t in, int32_t *left, int32_t *right);
/* The same frame with the receive register taking all 24 bits of `in`, as the
 * recorder feeds Q23 words past the A/D's word length. */
void ifx_dsp_board_frame_wide(ifx_dsp_board *board, uint32_t in, int32_t *left, int32_t *right);

unsigned ifx_dsp_board_receive_overruns(const ifx_dsp_board *board);
unsigned ifx_dsp_board_transmit_underruns(const ifx_dsp_board *board);
uint64_t ifx_dsp_board_frames(const ifx_dsp_board *board);
uint64_t ifx_dsp_board_passes(const ifx_dsp_board *board);
bool ifx_dsp_board_running(const ifx_dsp_board *board);

/* Memory as the core sees it: internal words, then the external array that P,
 * X and Y share, then the peripheral space at 0xffc0. `space` is a DSP_SPACE_*.
 * The read is a side-effect-free peek; the write takes the side effects a
 * running CPU would see, so a control register written here reconfigures its
 * peripheral. */
uint32_t ifx_dsp_board_word(const ifx_dsp_board *board, unsigned space, uint32_t address);
void ifx_dsp_board_store(ifx_dsp_board *board, unsigned space, uint32_t address, uint32_t value);

#endif /* IFX_DSP_BOARD_H */
