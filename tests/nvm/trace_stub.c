// Exercise the real tracer macros without a UART or target firmware.
#include <assert.h>
#include <stdint.h>
#include "sm_tracer.h"

extern unsigned Host_critical;
SM_Tracer SM_Tracer_inst;
static unsigned char frame[8];

unsigned char *SM_Trace_getBlock(SM_Tracer * const me) {
    assert(me == &SM_Tracer_inst && Host_critical == 0);
    return frame;
}

void SM_Trace_push(SM_Tracer * const me, unsigned char *bytes) {
    assert(me == &SM_Tracer_inst && bytes == frame);
    assert(frame[0] == 97 && frame[1] == 6);
    assert(frame[2] == 0 && frame[3] != 0);
    uint32_t const error = ((uint32_t)frame[4] << 24)
                         | ((uint32_t)frame[5] << 16)
                         | ((uint32_t)frame[6] << 8) | frame[7];
    assert((int32_t)error < 0);
}
