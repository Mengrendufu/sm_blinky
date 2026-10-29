#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bsp_fault_record.h"

static struct {
    uint32_t bootFlag;
    BSP_FaultRecord fault;
    uint32_t guard;
} retained;
static uint32_t BSP_dbgBootMs_[3];
#define BSP_TRACE_BOOT_TIMING 94U
static unsigned traces, position;
#define SM_TRACE_BEGIN(id, len) do { \
    assert((id) == BSP_TRACE_BOOT_TIMING && (len) == 4U); position = 0U;
#define SM_TRACE_PAYLOAD_BE(value) \
    (void)(value); ++position;
#define SM_TRACE_END(...) \
    assert(position == 4U); ++traces; } while (0);
#include "production.h"

static BSP_FaultStatus readRecord(BSP_FaultRecord *record) {
    return BSP_faultRecordRead_(&retained.fault, record);
}
static BSP_FaultRecord capture(uint32_t kind) {
    retained.fault = (BSP_FaultRecord){
        .pc = 0x08011234U, .lr = 0x08012345U, .exception = kind,
        .cfsr = 0x00010000U, .hfsr = 0x40000000U, .bfar = 0xDEADBEEFU,
        .flags = BSP_FAULT_FRAME_VALID, .exc_return = 0xFFFFFFFDU
    };
    retained.fault.seal = BSP_faultRecordSeal_(&retained.fault);
    return retained.fault;
}
int main(int argc, char **argv) {
    retained.bootFlag = 0xB007B007U;
    retained.guard = 0x12345678U;
    BSP_FaultRecord out;
    assert(readRecord(&out) == BSP_FAULT_EMPTY);
    retained.fault = (BSP_FaultRecord){
        .pc = 0x0AB2CDE0U, .lr = 0xDDE8DE98U, .exception = 0xA55AA55AU,
        .cfsr = 0xD786U, .hfsr = 0xDAE2U, .seal = 0xFFFFFFFFU
    };
    assert(readRecord(&out) == BSP_FAULT_INVALID);
    assert(retained.fault.pc == 0x0AB2CDE0U); // read never consumes
    BSP_faultRecordClear_(&retained.fault);
    assert(readRecord(&out) == BSP_FAULT_EMPTY);
    for (unsigned kind = 3U; kind <= 6U; ++kind) {
        BSP_FaultRecord const good = capture(kind);
        assert(readRecord(&out) == BSP_FAULT_VALID);
        assert(memcmp(&out, &good, sizeof(out)) == 0);
        assert(readRecord(&out) == BSP_FAULT_VALID);
        BSP_faultRecordClear_(&retained.fault);
        assert(readRecord(&out) == BSP_FAULT_EMPTY);
    }
    BSP_FaultRecord const good = capture(3U);
    for (unsigned byte = 0U; byte < sizeof(good); ++byte) {
        retained.fault = good;
        ((unsigned char *)&retained.fault)[byte] ^= 1U;
        assert(readRecord(&out) == BSP_FAULT_INVALID);
    }
    retained.fault = good;
    retained.fault.seal = 0U;
    assert(readRecord(&out) == BSP_FAULT_INVALID);
    retained.fault = good;
    retained.fault.pc = 0xDEADBEE0U;
    retained.fault.seal = BSP_faultRecordSeal_(&retained.fault);
    assert(readRecord(&out) == BSP_FAULT_VALID);
    retained.fault = good;
    retained.fault.exception = 15U;
    retained.fault.seal = BSP_faultRecordSeal_(&retained.fault);
    assert(readRecord(&out) == BSP_FAULT_INVALID);
    retained.fault = good;
    BSP_dbgBootReport();
    assert(traces == 1U);
    assert(memcmp(&retained.fault, &good, sizeof(good)) == 0);
    assert(retained.bootFlag == 0xB007B007U);
    assert(retained.guard == 0x12345678U);
    if (argc == 2) {
        FILE *f = fopen(argv[1], "rb");
        assert(f != NULL);
        assert(fread(&retained.fault, sizeof(retained.fault), 1U, f) == 1U);
        assert(fclose(f) == 0);
        assert(readRecord(&out) == BSP_FAULT_VALID);
        assert(out.pc == 0x106U && out.flags == BSP_FAULT_FRAME_VALID);
        assert(out.r4_r11[7] == 0xA00BU);
    }
    puts("PASS: cold/corrupt/partial records, four faults, explicit clear,");
    puts("      non-consuming reads, bad PC, bounds, no App consumption");
}
