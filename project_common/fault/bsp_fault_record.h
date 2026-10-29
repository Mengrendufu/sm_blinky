//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef BSP_FAULT_RECORD_H_
#define BSP_FAULT_RECORD_H_

#ifndef __ASSEMBLER__
#include <stdint.h>
#endif

// Shared with the stack-free Cortex-M3 writer. The boot flag keeps its
// original address; this record occupies a separate 128-byte NOLOAD area.
#define BSP_FAULT_SEAL_OFFSET 120
#define BSP_FAULT_PAYLOAD_WORDS 30
#define BSP_FAULT_SEED 0x46544C32
#define BSP_FAULT_FRAME_VALID 1

#ifndef __ASSEMBLER__
#include <stddef.h>

typedef struct {
    uint32_t pc, lr, exception, cfsr, hfsr, bfar;
    uint32_t r0, r1, r2, r3, r12, xpsr;
    uint32_t r4_r11[8];
    uint32_t msp, psp, exc_return, control;
    uint32_t primask, basepri, faultmask, mmfar, shcsr, flags;
    uint32_t seal;
} BSP_FaultRecord;

_Static_assert(sizeof(BSP_FaultRecord) == 124U, "fault record size");
_Static_assert(offsetof(BSP_FaultRecord, seal) == BSP_FAULT_SEAL_OFFSET,
               "assembly seal offset");
_Static_assert(offsetof(BSP_FaultRecord, r4_r11) == 48U,
               "assembly callee-saved offset");

typedef enum {
    BSP_FAULT_EMPTY,
    BSP_FAULT_VALID,
    BSP_FAULT_INVALID
} BSP_FaultStatus;

// Versioned, nonzero integrity seal over every payload word. This is not a
// cryptographic authenticator; it rejects uninitialized/corrupt RAM with a
// finite collision probability. Do not validate PC ranges: a real fault can
// be caused by an invalid branch target or corrupt stack.
static inline uint32_t BSP_faultRecordSeal_(BSP_FaultRecord const *r) {
    unsigned char const *bytes = (unsigned char const *)r;
    uint32_t h = BSP_FAULT_SEED;
    for (unsigned i = 0U; i < BSP_FAULT_PAYLOAD_WORDS; ++i) {
        uint32_t word;
        // Character access avoids aliasing across structure members.
        unsigned char *dst = (unsigned char *)&word;
        for (unsigned j = 0U; j < sizeof(word); ++j) {
            dst[j] = bytes[i * sizeof(word) + j];
        }
        h = (h ^ word) * 16777619U;
    }
    return h != 0U ? h : 0xFFFFFFFFU;
}

// Validate without consuming: bootloader clears only after durable commit.
static inline BSP_FaultStatus BSP_faultRecordStatus_(
    BSP_FaultRecord const *record) {
    unsigned char const *bytes = (unsigned char const *)record;
    unsigned char any = 0U;
    for (unsigned i = 0U; i < sizeof(*record); ++i) {
        any |= bytes[i];
    }
    if (any == 0U) {
        return BSP_FAULT_EMPTY;
    }
    if (record->seal != 0U && record->exception >= 3U
        && record->exception <= 6U
        && record->seal == BSP_faultRecordSeal_(record)) {
        return BSP_FAULT_VALID;
    }
    return BSP_FAULT_INVALID;
}

static inline BSP_FaultStatus BSP_faultRecordRead_(
    volatile BSP_FaultRecord const *stash, BSP_FaultRecord *record) {
    *record = *stash;
    return BSP_faultRecordStatus_(record);
}

static inline void BSP_faultRecordClear_(volatile BSP_FaultRecord *stash) {
    stash->seal = 0U;
    *stash = (BSP_FaultRecord){0};
}

#endif // !__ASSEMBLER__
#endif // BSP_FAULT_RECORD_H_
