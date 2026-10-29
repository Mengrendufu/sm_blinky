#define _GNU_SOURCE
#include "stm32f1xx_hal.h"
#include "bl_fault_log.h"
#include "project_layout.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

uint32_t Test_flashFlags;
static unsigned programs, erases, unlocks, uartCalls;
static unsigned failProgram, cutProgram;
static bool locked, failErase, cutErase, failUnlock, failUart;
static bool afterWriteError;
static jmp_buf powerCut;
static uint8_t wire[4096];
static unsigned wireSize;
static volatile BSP_FaultRecord * const stash =
    (volatile BSP_FaultRecord *)(uintptr_t)PROJECT_FAULT_BASE;

HAL_StatusTypeDef HAL_FLASH_Unlock(void) {
    ++unlocks;
    locked = failUnlock;
    return failUnlock ? HAL_ERROR : HAL_OK;
}
HAL_StatusTypeDef HAL_FLASH_Lock(void) {
    locked = true;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uint32_t address,
                                   uint64_t value) {
    assert(!locked && type == FLASH_TYPEPROGRAM_HALFWORD);
    assert(address >= PROJECT_LOG_BASE && address < PROJECT_FLASH_END);
    assert(address % 2U == 0U && value <= 0xFFFFU);
    ++programs;
    if (programs == failProgram && !afterWriteError) {
        return HAL_ERROR;
    }
    uint16_t *half = (uint16_t *)(uintptr_t)address;
    assert((*half & value) == value);
    *half = (uint16_t)value;
    if (programs == cutProgram) {
        longjmp(powerCut, 1);
    }
    return programs == failProgram ? HAL_ERROR : HAL_OK;
}
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *request,
                                   uint32_t *error) {
    assert(!locked && request->TypeErase == FLASH_TYPEERASE_PAGES);
    assert(request->NbPages == 1U);
    assert(request->PageAddress == PROJECT_LOG_BASE
           || request->PageAddress == PROJECT_LOG_BASE + FLASH_PAGE_SIZE);
    ++erases;
    memset((void *)(uintptr_t)request->PageAddress, 0xFF,
           (failErase || cutErase) ? FLASH_PAGE_SIZE / 2U : FLASH_PAGE_SIZE);
    if (cutErase) {
        longjmp(powerCut, 1);
    }
    *error = failErase ? request->PageAddress : 0xFFFFFFFFU;
    return failErase ? HAL_ERROR : HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart,
                                   uint8_t *data, uint16_t size,
                                   uint32_t timeout) {
    (void)uart;
    assert(timeout == 100U);
    ++uartCalls;
    if (failUart) {
        return HAL_ERROR;
    }
    assert(wireSize + size <= sizeof(wire));
    memcpy(wire + wireSize, data, size);
    wireSize += size;
    return HAL_OK;
}

static BSP_FaultRecord record(unsigned id) {
    BSP_FaultRecord r = {
        .exception = 3U, .pc = 0x08004321U, .lr = 0x08005679U,
        .flags = BSP_FAULT_FRAME_VALID, .exc_return = 0xFFFFFFFDU,
        .cfsr = 0x00010000U, .hfsr = 0x40000000U,
        .r0 = id, .r1 = 0x7E7D7E7DU
    };
    r.seal = BSP_faultRecordSeal_(&r);
    return r;
}
static void resetModel(void) {
    memset((void *)(uintptr_t)PROJECT_LOG_BASE, 0xFF, PROJECT_LOG_BYTES);
    BSP_faultRecordClear_(stash);
    Test_flashFlags = 0U;
    programs = erases = unlocks = uartCalls = wireSize = 0U;
    failProgram = cutProgram = 0U;
    locked = true;
    failErase = cutErase = failUnlock = failUart = afterWriteError = false;
}
static void assertLatest(BSP_FaultRecord const *expected, uint32_t seq) {
    BSP_FaultRecord out;
    uint32_t sequence;
    assert(Bl_faultLogLatest(&out, &sequence));
    assert(sequence == seq && memcmp(&out, expected, sizeof(out)) == 0);
}
static void dumpWire(char const *folder, char const *name) {
    char path[1024];
    assert(snprintf(path, sizeof(path), "%s/%s", folder, name) > 0);
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    assert(fwrite(wire, 1U, wireSize, f) == wireSize);
    assert(fclose(f) == 0);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    assert(mmap((void *)(uintptr_t)PROJECT_LOG_BASE, PROJECT_LOG_BYTES,
                PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS
                | MAP_FIXED_NOREPLACE, -1, 0)
           == (void *)(uintptr_t)PROJECT_LOG_BASE);
    assert(mmap((void *)0x2000F000U, 0x1000U, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0)
           == (void *)0x2000F000U);
    BSP_FaultRecord a = record(1U), b = record(2U), out;
    uint32_t seq;
    UART_HandleTypeDef uart = {0};
    resetModel();
    assert(!Bl_faultBoot(&uart));
    assert(programs == 0U && erases == 0U && wireSize == 0U);
    assert(!Bl_faultLogLatest(&out, &seq));
    memset((void *)stash, 0xA5, sizeof(*stash));
    assert(!Bl_faultBoot(&uart));
    assert(BSP_faultRecordRead_(stash, &out) == BSP_FAULT_EMPTY);
    assert(programs == 0U && erases == 0U);
    dumpWire(argv[1], "invalid.bin");

    resetModel();
    *stash = a;
    assert(Bl_faultBoot(&uart));
    assertLatest(&a, 1U);
    assert(programs == 68U && erases == 0U && locked);
    assert(BSP_faultRecordRead_(stash, &out) == BSP_FAULT_EMPTY);
    dumpWire(argv[1], "saved.bin");
    unsigned writes = programs;
    *stash = a;
    wireSize = 0U;
    assert(Bl_faultBoot(&uart));
    assert(programs == writes && erases == 0U);
    assert(BSP_faultRecordRead_(stash, &out) == BSP_FAULT_EMPTY);
    dumpWire(argv[1], "duplicate.bin");
    wireSize = 0U;
    assert(!Bl_faultBoot(&uart));
    assert(programs == writes && erases == 0U);
    dumpWire(argv[1], "replay.bin");
    // Same PC/status but a different register must produce a new record.
    assert(Bl_faultLogSave(&b) == BL_FAULT_SAVED);
    assertLatest(&b, 2U);
    assert(Bl_faultLogSave(&a) == BL_FAULT_SAVED);
    assertLatest(&a, 3U); // A -> B -> A preserves distinct transitions
    a.seal ^= 1U;
    writes = programs;
    assert(Bl_faultLogSave(&a) == BL_FAULT_SAVE_FAILED);
    assert(programs == writes);
    a = record(1U);

    // Every halfword boundary, including a fully written commit marker.
    for (unsigned cut = 1U; cut <= 68U; ++cut) {
        resetModel();
        assert(Bl_faultLogSave(&a) == BL_FAULT_SAVED);
        *stash = b;
        cutProgram = programs + cut;
        if (setjmp(powerCut) == 0) {
            Bl_faultBoot(&uart);
            assert(0 && "power cut must interrupt persistence");
        }
        locked = true; // hardware reset
        cutProgram = 0U;
        assert(BSP_faultRecordRead_(stash, &out) == BSP_FAULT_VALID);
        assert(memcmp(&out, &b, sizeof(b)) == 0);
        if (cut < 68U) {
            assertLatest(&a, 1U);
        }
        else {
            assertLatest(&b, 2U);
        }
        wireSize = 0U;
        unsigned before = programs;
        Bl_faultBoot(&uart);
        assertLatest(&b, 2U);
        assert(BSP_faultRecordRead_(stash, &out) == BSP_FAULT_EMPTY);
        if (cut == 68U) {
            assert(programs == before); // commit-before-clear dedup
        }
    }

    resetModel();
    assert(Bl_faultLogSave(&a) == BL_FAULT_SAVED);
    *stash = b;
    failProgram = programs + 3U;
    assert(Bl_faultBoot(&uart));
    assertLatest(&a, 1U);
    assert(BSP_faultRecordRead_(stash, &out) == BSP_FAULT_VALID);
    dumpWire(argv[1], "failed.bin");
    failProgram = 0U;
    wireSize = 0U;
    failUart = true;
    assert(Bl_faultBoot(&uart));
    assertLatest(&b, 2U); // UART failure does not roll back durable record
    assert(BSP_faultRecordRead_(stash, &out) == BSP_FAULT_EMPTY);
    failUart = false;

    // Thirty slots are used before the first erase. Rollover keeps latest.
    resetModel();
    for (unsigned i = 1U; i <= 30U; ++i) {
        BSP_FaultRecord r = record(i);
        assert(Bl_faultLogSave(&r) == BL_FAULT_SAVED);
        assertLatest(&r, i);
        assert(erases == 0U);
    }
    BSP_FaultRecord r30 = record(30U), r31 = record(31U);
    *stash = r31;
    failErase = true;
    wireSize = 0U;
    Bl_faultBoot(&uart);
    assertLatest(&r30, 30U);
    assert(BSP_faultRecordRead_(stash, &out) == BSP_FAULT_VALID);
    failErase = false;
    wireSize = 0U;
    Bl_faultBoot(&uart);
    assertLatest(&r31, 31U);
    // Half-erased old page has blank slots usable without another erase.
    for (unsigned i = 32U; i <= 100U; ++i) {
        BSP_FaultRecord r = record(i);
        assert(Bl_faultLogSave(&r) == BL_FAULT_SAVED);
        assertLatest(&r, i);
    }
    assert(erases < 10U);

    // Power loss during erase never destroys the latest page.
    resetModel();
    for (unsigned i = 1U; i <= 30U; ++i) {
        BSP_FaultRecord r = record(i);
        assert(Bl_faultLogSave(&r) == BL_FAULT_SAVED);
    }
    *stash = r31;
    cutErase = true;
    if (setjmp(powerCut) == 0) {
        Bl_faultBoot(&uart);
        assert(0 && "erase power cut must interrupt persistence");
    }
    locked = true;
    cutErase = false;
    assertLatest(&r30, 30U);
    assert(BSP_faultRecordRead_(stash, &out) == BSP_FAULT_VALID);
    wireSize = 0U;
    Bl_faultBoot(&uart);
    assertLatest(&r31, 31U);

    // A HAL error after the commit write is still deduplicated on reboot.
    resetModel();
    *stash = a;
    failProgram = 68U;
    afterWriteError = true;
    Bl_faultBoot(&uart);
    assertLatest(&a, 1U);
    assert(BSP_faultRecordRead_(stash, &out) == BSP_FAULT_VALID);
    writes = programs;
    failProgram = 0U;
    wireSize = 0U;
    Bl_faultBoot(&uart);
    assert(programs == writes);
    assert(BSP_faultRecordRead_(stash, &out) == BSP_FAULT_EMPTY);

    resetModel();
    *stash = a;
    Bl_faultBoot(NULL); // burn mode must not emit ACK/NAK-looking bytes
    assertLatest(&a, 1U);
    assert(wireSize == 0U && uartCalls == 0U);
    assert(BSP_faultRecordRead_(stash, &out) == BSP_FAULT_EMPTY);

    resetModel();
    *stash = a;
    failUnlock = true;
    Bl_faultBoot(&uart);
    assert(BSP_faultRecordRead_(stash, &out) == BSP_FAULT_VALID);
    assert(programs == 0U && erases == 0U);
    failUnlock = false;
    Test_flashFlags = FLASH_FLAG_BSY;
    wireSize = 0U;
    Bl_faultBoot(&uart);
    assert(BSP_faultRecordRead_(stash, &out) == BSP_FAULT_VALID);
    assert(programs == 0U && erases == 0U);
    puts("PASS: exact dedup, append/rotation, 68 power-cut boundaries,");
    puts("      failure retention, commit-before-clear, UART independence");
    return 0;
}
