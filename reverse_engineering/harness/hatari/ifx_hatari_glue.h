/* Stands in for the Hatari runtime the vendored DSP56001 core was built on:
 * tracing, logging, the debugger, the profiler and the Falcon's ioMem/dsp glue.
 * Every one of them is off, and a warning goes to stderr. */
#ifndef IFX_HATARI_GLUE_H
#define IFX_HATARI_GLUE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
	TRACE_DSP_STATE,
	TRACE_DSP_HOST_INTERFACE,
	TRACE_DSP_HOST_COMMAND,
	TRACE_DSP_HOST_SSI,
	TRACE_DSP_DISASM,
	TRACE_DSP_DISASM_REG,
	TRACE_DSP_DISASM_MEM,
	TRACE_DSP_INTERRUPT
};

enum {
	LOG_DEBUG,
	LOG_INFO,
	LOG_WARN,
	LOG_ERROR
};

#define TraceFile stderr
#define LOG_TRACE(level, ...) ((void)0)
#define LOG_TRACE_LEVEL(level) (0)
#define LOG_TRACE_DIRECT_INIT() ((void)0)
#define Log_Printf(level, ...) fprintf(stderr, __VA_ARGS__)
#define DebugUI(reason) ((void)0)

/* Hatari's crossbar/DMA handshake hooks: no crossbar on this board. */
static inline void DSP_SsiTransmit_SC1(void) {}
static inline void DSP_SsiTransmit_SC2(uint32_t value) { (void)value; }

/* The debugger's exception mask and the reason it opens on. */
#define ExceptionDebugMask 0
#define EXCEPT_DSP 0

static inline int Profile_DspAddressData(uint32_t pc, float *percentage, uint64_t *count,
                                         uint64_t *cycles, uint16_t *cycle_diff)
{
	(void)pc;
	(void)percentage;
	(void)count;
	(void)cycles;
	(void)cycle_diff;
	return 0;
}

#endif /* IFX_HATARI_GLUE_H */
