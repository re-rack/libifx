/* The Intellifex LTD native effects: the one header a host includes, and the
 * contract every effect follows.
 *
 * Effects are mono in, stereo out. The DSP sees one Q23 input word per sample
 * period (a signed 24-bit value in an int32_t; higher bits are ignored) and
 * writes a left and a right word. Every effect adds one block of latency:
 * output frame m is the device's output for input frame m - 32, and the first
 * 32 frames are the zero rest output. Effects run at the device's rate,
 * IFX_DEVICE_RATE, with every time a fixed sample count: a host at another
 * rate resamples to it. Setup returns -1 for a NULL pointer. A knob change is
 * a new config passed with the next process call; it applies from the next
 * frame.
 *
 * The split and float entry points take a mono input and two output buffers.
 * Either output may be the input buffer, as hosts process in place; no other
 * overlap is allowed. The interleaved entry point writes L,R pairs and allows
 * no overlap because its output is twice its input. Nothing allocates. The
 * library keeps no global state: different objects may be used from different
 * threads, but one object must not be written while another thread uses it.
 */
#ifndef IFX_H
#define IFX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "ifx_base.h"
#include "ifx_chorus8.h"
#include "ifx_chorus8_setup.h"
#include "ifx_chorus_reverb.h"
#include "ifx_chorus_reverb_setup.h"
#include "ifx_convert.h"
#include "ifx_delay.h"
#include "ifx_delay_setup.h"
#include "ifx_pitch.h"
#include "ifx_pitch_reverb.h"
#include "ifx_pitch_reverb_setup.h"
#include "ifx_pitch_setup.h"
#include "ifx_reverb.h"
#include "ifx_reverb_setup.h"

#ifdef __cplusplus
}
#endif

#endif /* IFX_H */
