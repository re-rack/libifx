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

/* One frame is one input sample: 33 MHz of DSP clock over 39.0625 kHz. */
#define IFX_DSP_BOARD_FRAME_CYCLES 423u
/* 16-bit words for the A/D and the DAC on one frame sync. */
#define IFX_DSP_BOARD_SSI_CLOCKS 32u

enum ifx_dsp_port { IFX_DSP_PORT_B = 0, IFX_DSP_PORT_C = 1 };

typedef struct ifx_dsp_board ifx_dsp_board;

/* One board per process: the core keeps its own global state and reports its
 * host interrupt through a plain function pointer. */
ifx_dsp_board *ifx_dsp_board_create(void);
void ifx_dsp_board_destroy(ifx_dsp_board *board);
void ifx_dsp_board_reset(ifx_dsp_board *board);

/* MCU side of the host port. `reg` is a CPU_HOST_* register of the core's
 * header; ICR, CVR and the transmit bytes are writable, ISR is read only. */
void ifx_dsp_board_host_write(ifx_dsp_board *board, unsigned reg, uint8_t value);
uint8_t ifx_dsp_board_host_read(const ifx_dsp_board *board, unsigned reg);
bool ifx_dsp_board_host_transmit_ready(const ifx_dsp_board *board); /* ISR TXDE */
bool ifx_dsp_board_host_receive_ready(const ifx_dsp_board *board);  /* ISR RXDF */

/* One host word as TRXH, TRXM, TRXL: the order C:0FA4 sends them, and the one
 * the bootstrap loader and the receive vector expect. */
void ifx_dsp_board_host_send(ifx_dsp_board *board, uint32_t word);
/* RXH, RXM, RXL; RXDF clears on the low byte, as on the part. */
uint32_t ifx_dsp_board_host_receive(const ifx_dsp_board *board);

/* The interrupt the host interface raises towards the MCU, cleared by the next
 * read or write that leaves ICR and ISR without a pending request. */
bool ifx_dsp_board_host_request(const ifx_dsp_board *board);

/* Run the core: one instruction, or whole instructions until at least `cycles`
 * have elapsed. Nothing runs while the part waits for its bootstrap. */
void ifx_dsp_board_step(ifx_dsp_board *board);
void ifx_dsp_board_run(ifx_dsp_board *board, uint32_t cycles);

/* One SSI frame: hand `in` to the receiver, run a sample period of
 * instructions, then clock out the two words the DAC takes per frame. A frame
 * that found the receiver still full or the transmitter still empty is counted
 * and, for the transmitter, repeats the previous word.
 *
 * `in` is the receive register's word as the A/D's converter fills it; `left`
 * and `right` are the DSP's own 24-bit transmit words, taken before the SSI
 * shifts them to its word length, and signed. */
void ifx_dsp_board_frame(ifx_dsp_board *board, uint32_t in, int32_t *left, int32_t *right);

/* The two words the last frame clocked out. */
void ifx_dsp_board_output(const ifx_dsp_board *board, int32_t *left, int32_t *right);

unsigned ifx_dsp_board_receive_overruns(const ifx_dsp_board *board);
unsigned ifx_dsp_board_transmit_underruns(const ifx_dsp_board *board);
uint64_t ifx_dsp_board_frames(const ifx_dsp_board *board);
uint64_t ifx_dsp_board_cycles(const ifx_dsp_board *board);
bool ifx_dsp_board_running(const ifx_dsp_board *board);

/* Memory as the core sees it: internal words, then the external array that P,
 * X and Y share, then the peripheral space at 0xffc0. `space` is a DSP_SPACE_*.
 * The read is a side-effect-free peek; the write takes the side effects a
 * running CPU would see, so a control register written here reconfigures its
 * peripheral. */
uint32_t ifx_dsp_board_word(const ifx_dsp_board *board, unsigned space, uint32_t address);
void ifx_dsp_board_store(ifx_dsp_board *board, unsigned space, uint32_t address, uint32_t value);

/* Registers the board owns rather than the core's datapath. BCR's wait states
 * and IPR's interrupt levels are static on this board: no device drives IRQA,
 * IRQB or the bus. */
uint32_t ifx_dsp_board_omr(const ifx_dsp_board *board);
void ifx_dsp_board_set_omr(ifx_dsp_board *board, uint32_t value);
uint32_t ifx_dsp_board_bcr(const ifx_dsp_board *board);
void ifx_dsp_board_set_bcr(ifx_dsp_board *board, uint32_t value);
uint32_t ifx_dsp_board_ipr(const ifx_dsp_board *board);
void ifx_dsp_board_set_ipr(ifx_dsp_board *board, uint32_t value);

/* Port B and C control and data registers (DSP_PBC..DSP_PCD) and the pin
 * levels the board drives: an output pin reads back its data bit, an input
 * pin reads low since nothing drives it. */
uint32_t ifx_dsp_board_port_register(const ifx_dsp_board *board, unsigned reg);
unsigned ifx_dsp_board_port_pins(const ifx_dsp_board *board, enum ifx_dsp_port port);

/* CPU registers by DSP_REG_* number. */
uint32_t ifx_dsp_board_reg(const ifx_dsp_board *board, unsigned reg);
void ifx_dsp_board_set_reg(ifx_dsp_board *board, unsigned reg, uint32_t value);

#endif /* IFX_DSP_BOARD_H */
