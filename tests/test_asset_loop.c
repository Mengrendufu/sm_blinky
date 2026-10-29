// Host integration: production boot protocol + loader, mocked UART/NOR.
// Run through test_asset_loop.py; it generates script-produced packages.
#define _GNU_SOURCE
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include BOOTLOADER_SOURCE
#include "../app/application/asset.c"
#include "hdlc_frame.h"
#include "hdlc_parser.h"

uint32_t _snoinit;
BSP_flashOperations const BSP_flashDmaOperations = {0};
static BSP_Flash device;
static uint8_t nor[0x140000];
static uint8_t stream[2U * ASSET_FORMAT_MAX_PACKAGE + 32768U];
static HdlcParser replies;
static uint8_t txSequence;
static FILE *capturedWire;
static uint32_t gapAt;
static unsigned gapCount;
static uint32_t inputLen, inputPos, ackCount, nakCount, writes, erases;
static uint32_t reads[3], headerReads;
static int faultSlot = -1, faultCount;
static bool failRead;
static bool bootWindowTest;
static uint32_t testTick;
static UART_HandleTypeDef testUart;
static bool retainedFault;
static TestSCB testScb;
static TestNVIC testNvic;
TestSCB *SCB = &testScb;
TestNVIC *NVIC = &testNvic;
bool Bl_faultBoot(UART_HandleTypeDef *uart) {
    (void)uart;
    return retainedFault;
}
void HAL_DeInit(void) {}
int HAL_UART_DeInit(UART_HandleTypeDef *uart) {
    (void)uart;
    return HAL_OK;
}
static jmp_buf stopped;

void SM_onAssert(char const *module, int label) {
    fprintf(stderr, "assertion: %s:%d\n", module, label);
    abort();
}

BSP_Flash *BSP_flashDevice(void) { return &device; }
void BSP_flashInit(void) {}
void BSP_flashBind(BSP_Flash *me, BSP_flashOperations const *operations,
                   BSP_flashWaitOperations const *wait, void *context) {
    (void)me; (void)operations; (void)wait; (void)context;
}
bool BSP_flashRead(BSP_Flash *me, uint32_t address, void *dst,
                    uint32_t len) {
    (void)me;
    assert(address + len <= sizeof nor);
    if (len == ASSET_SLOT_BYTES) {
        uint32_t const slot = (address - NOR_ASSET_DATA) / ASSET_SLOT_BYTES;
        assert(slot < 3U);
        ++reads[slot];
        if ((int)slot == faultSlot && faultCount != 0) {
            if (faultCount > 0) { --faultCount; }
            if (failRead) { return false; }
            memcpy(dst, &nor[address], len);
            ((uint8_t *)dst)[123] ^= 1U;
            return true;
        }
    }
    if (address == NOR_ASSET_BASE && len == ASSET_FORMAT_HEADER) {
        ++headerReads;
    }
    memcpy(dst, &nor[address], len);
    return true;
}
bool BSP_flashWrite(BSP_Flash *me, uint32_t address, void const *src,
                     uint32_t len) {
    (void)me;
    assert(address >= NOR_ASSET_BASE);
    assert(address + len <= NOR_ASSET_END);
    ++writes;
    for (uint32_t i = 0; i < len; ++i) {
        nor[address + i] &= ((uint8_t const *)src)[i];
    }
    return true;
}
bool BSP_flashEraseSector(BSP_Flash *me, uint32_t address) {
    (void)me;
    assert(address % 4096U == 0U);
    assert(address >= NOR_ASSET_BASE);
    assert(address + 4096U <= NOR_NVS_BASE);
    ++erases;
    memset(&nor[address], 0xFF, 4096);
    return true;
}
uint32_t HAL_GetTick(void) { return testTick; }
void HAL_Delay(uint32_t ms) { (void)ms; }
_Noreturn void NVIC_SystemReset(void) { longjmp(stopped, 2); }
int HAL_UART_Receive(UART_HandleTypeDef *uart, uint8_t *data,
                     uint16_t len, uint32_t timeout) {
    (void)uart;
    if (bootWindowTest) {
        testTick += timeout;
        if (inputPos + len > inputLen) { return 1; }
    }
    if (gapCount != 0U && inputPos == gapAt) {
        --gapCount;
        return 1; // HAL timeout: the resync state observes a quiet line
    }
    if (inputPos + len > inputLen) { longjmp(stopped, 1); }
    memcpy(data, stream + inputPos, len);
    inputPos += len;
    return HAL_OK;
}
static void captureReply(void *context, uint8_t const *frame, size_t size) {
    (void)context;
    if (frame[1] != ASSET_LINK_REPLY || size != 7U) {
        return;
    }
    if (frame[6] == BL_ACK_MAGIC) { ++ackCount; }
    if (frame[6] == BL_NAK_MAGIC) { ++nakCount; }
}
int HAL_UART_Transmit(UART_HandleTypeDef *uart, uint8_t *data,
                      uint16_t len, uint32_t timeout) {
    (void)uart; (void)timeout;
    if (capturedWire != NULL) {
        uint8_t const log[] = {0U, 0x55U, 0x4EU, 0x7DU, 0x7EU};
        uint8_t wire[22];
        size_t const n = HdlcFrame_encode(1U, 116U, log, sizeof(log),
                                         wire, sizeof(wire));
        assert(fwrite(wire, 1U, n, capturedWire) == n);
        assert(fwrite(data, 1U, len, capturedWire) == len);
    }
    for (unsigned i = 0U; i < len; ++i) {
        HdlcParser_input(&replies, data[i]);
    }
    return HAL_OK;
}

static void append(void const *bytes, uint32_t len) {
    assert(inputLen + len <= sizeof stream);
    memcpy(stream + inputLen, bytes, len);
    inputLen += len;
}
static void packet(void const *data, uint16_t size) {
    uint8_t wire[30];
    size_t const count = HdlcFrame_encode(txSequence++, ASSET_LINK_REQUEST,
        data, size, wire, sizeof(wire));
    assert(count != 0U);
    append(wire, count);
}
static void handshake(uint32_t len) {
    uint8_t hdr[10] = {BL_REQ_MAGIC, BL_PROTO_VER};
    Bl_putBe32_(hdr + 2, len);
    Bl_putBe32_(hdr + 6, Bl_crc32_(hdr + 1, 5));
    packet(hdr, sizeof hdr);
}
static void testBootWindow(uint32_t len) {
    bootWindowTest = true;
    for (unsigned mode = 0U; mode < 10U; ++mode) {
        testTick = mode == 9U ? UINT32_MAX - 100U : 0U;
        uint32_t const start = testTick;
        inputPos = inputLen = Bl_len_ = Bl_frames_ = Bl_ok_ = 0U;
        if (mode != 0U) { handshake(len); }
        if (mode == 2U) { stream[inputLen - 2U] ^= 1U; }
        if (mode == 3U) { --inputLen; } // Missing closing flag.
        if (mode == 4U) {
            memset(stream, 0x55, 300U);
            inputLen = 300U; // Noise must not extend the deadline.
        }
        if (mode == 5U) {
            inputLen = 0U;
            uint8_t bad[10] = {BL_REQ_MAGIC, BL_PROTO_VER + 1U};
            Bl_putBe32_(bad + 2U, len);
            Bl_putBe32_(bad + 6U, Bl_crc32_(bad + 1U, 5U));
            packet(bad, sizeof(bad));
        }
        if (mode == 6U) {
            inputLen = 0U;
            uint8_t block[7] = {BL_BLK_MAGIC, 0U, 0U, 0x10U};
            packet(block, sizeof(block));
        }
        if (mode == 7U) {
            stream[inputLen - 2U] ^= 1U;
            handshake(len); // Bad frame followed by a valid retry.
        }
        if (mode == 8U) {
            memmove(stream + 195U, stream, inputLen);
            memset(stream, 0x55, 195U);
            inputLen += 195U; // HELLO cannot finish before the deadline.
        }
        _snoinit = 0U;
        SM_Hsm_init_(&Bl_boot_.hsm, (SM_InitHandler)Bl_bootInitial_);
        assert(Bl_boot_.hsm.curr == &Bl_bootHandshake);
        while (Bl_boot_.hsm.curr == &Bl_bootHandshake) {
            Bl_TransferEvt event = {.sig = BL_BYTE_SIG};
            if (HAL_UART_Receive(Bl_uart_, &event.byte, 1U,
                                 Bl_boot_.receiveTimeout) != HAL_OK) {
                event.sig = BL_GAP_SIG;
            }
            Bl_bootDispatch_(&event);
        }
        bool const accepted = Bl_boot_.hsm.curr == &Bl_bootAssets;
        if (!accepted) {
            assert(Bl_boot_.hsm.curr == &Bl_bootInvalidApp);
        }
        assert(accepted == (mode == 1U || mode == 7U || mode == 9U));
        assert((uint32_t)(testTick - start) <= BL_BOOT_MS);
        assert(writes == 0U && erases == 0U && ackCount == 0U);
        assert(Bl_len_ == (accepted ? len : 0U));
        assert(Bl_ok_ == (accepted ? 1U : 0U));
    }
    // Retained faults take priority over forced asset entry; stopped states
    // absorb commands without entering any transfer context.
    _snoinit = BL_FLAG_MAGIC;
    retainedFault = true;
    SM_Hsm_init_(&Bl_boot_.hsm, (SM_InitHandler)Bl_bootInitial_);
    assert(Bl_boot_.hsm.curr == &Bl_bootFault);
    Bl_TransferEvt const event = {.sig = BL_BOOT_COMMAND_SIG};
    Bl_bootDispatch_(&event);
    assert(Bl_boot_.hsm.curr == &Bl_bootFault);
    retainedFault = false;
    Bl_len_ = 0U;
    SM_Hsm_init_(&Bl_boot_.hsm, (SM_InitHandler)Bl_bootInitial_);
    assert(Bl_boot_.hsm.curr == &Bl_bootAssets);
    // Check the valid-App timeout target without executing the host's
    // hardware handover action. Invalid vectors ran through full dispatch.
    uint32_t *vectors = (uint32_t *)(uintptr_t)BL_APP_BASE;
    vectors[0] = PROJECT_FAULT_BASE;
    vectors[1] = BL_APP_BASE + 1U;
    _snoinit = 0U;
    SM_Hsm_init_(&Bl_boot_.hsm, (SM_InitHandler)Bl_bootInitial_);
    testTick += BL_BOOT_MS;
    Bl_TransferEvt const timeout = {.sig = BL_GAP_SIG};
    assert(Bl_bootHandshake_(&Bl_boot_.hsm, &timeout) == SM_RET_TRAN);
    assert(Bl_boot_.hsm.next == &Bl_bootApp);
    puts("PASS boot stages: HELLO routing, deadline, fault hold, flag entry");
}
static void block(uint8_t const *pack, uint32_t index, uint32_t len) {
    uint8_t hdr[7] = {BL_BLK_MAGIC, index >> 8, index,
                     len >> 8, len, 0, 0};
    uint32_t const crc = Bl_crc32_(pack, len);
    hdr[5] = crc >> 8; hdr[6] = crc;
    packet(hdr, sizeof hdr);
    append(pack, len);
}
static void finish(uint8_t const *pack, uint32_t len, bool corrupt) {
    uint8_t hdr[9] = {BL_FIN_MAGIC};
    Bl_putBe32_(hdr + 1, len);
    Bl_putBe32_(hdr + 5, Bl_crc32_(pack, len));
    if (corrupt) { hdr[8] ^= 1U; }
    packet(hdr, sizeof hdr);
}

static void inputEvent(uint8_t sig, uint8_t byte) {
    Bl_TransferEvt event = {.sig = sig, .byte = byte};
    SM_Hsm_dispatch_(&Bl_transfer_.hsm, &event);
    if (Bl_transfer_.pending) {
        Bl_transfer_.pending = false;
        event.sig = Bl_transfer_.command;
        SM_Hsm_dispatch_(&Bl_transfer_.hsm, &event);
    }
}
static void feedControl(uint8_t const *packet, uint8_t size, bool bad) {
    uint8_t wire[30];
    size_t n = HdlcFrame_encode(0U, ASSET_LINK_REQUEST, packet, size,
                                wire, sizeof(wire));
    if (bad) { wire[n - 2U] ^= 1U; }
    for (size_t i = 0U; i < n; ++i) { inputEvent(BL_BYTE_SIG, wire[i]); }
}
static void testContexts(uint32_t len) {
    HdlcParser_ctor(&Bl_transfer_.parser, &Bl_controlFrame_, NULL);
    SM_Hsm_init_(&Bl_transfer_.hsm, (SM_InitHandler)Bl_transferInitial_);
    uint8_t hello[10] = {BL_REQ_MAGIC, BL_PROTO_VER};
    Bl_putBe32_(hello + 2U, len);
    Bl_putBe32_(hello + 6U, Bl_crc32_(hello + 1U, 5U));
    feedControl(hello, sizeof(hello), false);
    assert(Bl_transfer_.hsm.curr == &Bl_control && Bl_frames_ == 1U);
    uint8_t block[7] = {BL_BLK_MAGIC, 0U, 0U, 0x10U, 0U, 0U, 0U};
    feedControl(block, sizeof(block), false);
    assert(Bl_transfer_.hsm.curr == &Bl_data);
    // Valid-looking commands in data never reach the control parser.
    feedControl(hello, sizeof(hello), false);
    assert(Bl_transfer_.hsm.curr == &Bl_data && Bl_frames_ == 1U);
    inputEvent(BL_GAP_SIG, 0U);
    assert(Bl_transfer_.hsm.curr == &Bl_resync);
    feedControl(hello, sizeof(hello), false);
    assert(Bl_frames_ == 1U && writes == 0U && erases == 0U);
    inputEvent(BL_GAP_SIG, 0U);
    assert(Bl_transfer_.hsm.curr == &Bl_control && nakCount == 1U);
    feedControl(hello, sizeof(hello), true);
    assert(Bl_transfer_.hsm.curr == &Bl_resync);
    feedControl(hello, sizeof(hello), false);
    assert(Bl_frames_ == 1U);
    inputEvent(BL_GAP_SIG, 0U);
    assert(Bl_transfer_.hsm.curr == &Bl_control);
}

int main(int argc, char **argv) {
    assert(argc == 3 || argc == 5 || argc == 6);
    FILE *file = fopen(argv[1], "rb");
    assert(file);
    uint8_t *pack = malloc(ASSET_FORMAT_MAX_PACKAGE);
    assert(pack);
    uint32_t const len = fread(pack, 1, ASSET_FORMAT_MAX_PACKAGE, file);
    fclose(file);
    assert(AssetFormat_valid(pack));
    int const mode = atoi(argv[2]);
    Bl_uart_ = &testUart;
    HdlcParser_ctor(&replies, &captureReply, NULL);
    HdlcParser_init(&replies);
    memset(nor, 0xFF, sizeof nor);
    memset(nor + NOR_NVS_BASE, 0xA5, NOR_NVS_SIZE);
    assert(mmap((void *)(uintptr_t)ASSET_SRAM, 0x100000,
                PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS
                | MAP_FIXED_NOREPLACE, -1, 0) != MAP_FAILED);
    assert(mmap((void *)(uintptr_t)BL_APP_BASE, 4096U,
                PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS
                | MAP_FIXED_NOREPLACE, -1, 0) != MAP_FAILED);
    if (mode == 18) {
        testBootWindow(len);
        free(pack);
        return 0;
    }
    if (mode == 17) {
        testContexts(len);
        free(pack);
        puts("PASS control/data/resync ownership and gap transitions");
        return 0;
    }
    if (mode == 8) { pack[ASSET_FORMAT_SECTOR + 123] ^= 1; }
    if (mode == 9) { pack[16 + 12] ^= 1; }
    if (mode == 12) {
        Bl_putBe32_(pack + 20, 0x100000U); // Resealed out-of-range entry.
        Bl_putBe32_(pack + 64, Bl_crc32_(pack, 64));
    }
    if (mode == 11) { // Replacement starts from a previously valid pack.
        memcpy(nor + NOR_ASSET_BASE, pack, ASSET_FORMAT_HEADER);
    }
    if (mode != 15) { handshake(len); }
    if (mode == 10) { // Invalid index must never erase NVS.
        block(pack, 127, 4096);
    } else {
        for (uint32_t off = 0; off < len; off += 4096) {
            uint32_t const n = len - off > 4096 ? 4096 : len - off;
            block(pack + off, off / 4096, n);
            if ((mode == 1 && off == 4096)
                || (mode == 13 && off == 0)
                || (mode == 16 && off + n == len)) {
                block(pack + off, off / 4096, n); // Lost ACK replay.
            }
            if (mode == 11 && off == 4096) { break; }
        }
        if (mode != 11) {
            finish(pack, len, mode == 14);
        }
    }
    if (argc >= 5) {
        FILE *input = fopen(argv[3], "rb");
        assert(input != NULL);
        inputLen = fread(stream, 1U, sizeof(stream), input);
        assert(feof(input) && fclose(input) == 0);
        if (argc == 6) {
            gapAt = (uint32_t)strtoul(argv[5], NULL, 10);
            gapCount = 1U;
        }
        capturedWire = fopen(argv[4], "wb");
        assert(capturedWire != NULL);
    }
    int const result = setjmp(stopped);
    if (result == 0) {
        if (mode == 19) {
            bootWindowTest = true;
            _snoinit = 0U;
        }
        else { _snoinit = BL_FLAG_MAGIC; }
        Bl_Boot_run(&testUart);
    }
    if (capturedWire != NULL) {
        assert(fclose(capturedWire) == 0);
        capturedWire = NULL;
    }
    for (uint32_t i = 0; i < NOR_NVS_SIZE; ++i) {
        assert(nor[NOR_NVS_BASE + i] == 0xA5);
    }
    if ((mode >= 8 && mode <= 12) || mode == 14 || mode == 15) {
        assert(result == 1 && Bl_final_ != 1);
        assert(!AssetFormat_valid(nor + NOR_ASSET_BASE));
        if (mode == 10) { assert(erases == 0 && writes == 0); }
        free(pack);
        printf("PASS boot rejection/interruption %d\n", mode);
        return 0;
    }
    assert(result == 2 && Bl_final_ == 1);
    assert(Bl_frames_ == 1U); // valid-looking frames inside assets stay raw
    assert(Bl_transfer_.hsm.curr == &Bl_control);
    assert(Bl_bytes_ == len);
    assert(Bl_blocks_ == (len + 4095) / 4096);
    assert(ackCount == Bl_blocks_ + 1U
           + (mode == 1 || mode == 13 || mode == 16));
    assert(nakCount == 0);
    assert(memcmp(nor + NOR_ASSET_BASE, pack, ASSET_FORMAT_HEADER) == 0);
    assert(memcmp(nor + NOR_ASSET_DATA, pack + 4096, len - 4096) == 0);
    if (mode == 2) { faultSlot = 2; faultCount = 1; }
    if (mode == 3) { faultSlot = 2; faultCount = -1; }
    if (mode == 4) { faultSlot = 0; faultCount = -1; }
    if (mode == 5) { faultSlot = 1; faultCount = 2; failRead = true; }
    if (mode == 6) { nor[NOR_ASSET_BASE + 20] ^= 1; }
    if (mode == 7) { nor[NOR_ASSET_BASE + 3] = '1'; }
    Asset_loadSplash();
    if (mode == 6 || mode == 7) {
        assert(Asset_splash() == NULL && Asset_bytes() == 0);
        assert(headerReads >= 3);
        Asset_loadRemaining();
        assert(!Asset_get()->present);
    } else {
        bool const hasSplash = (len - 4096) / ASSET_SLOT_BYTES > 2;
        assert((Asset_splash() != NULL) == (hasSplash && mode != 3));
        uint32_t const splashReads = reads[2];
        Asset_loadRemaining();
        assert(reads[2] == splashReads);
        assert(Asset_get()->present == (mode != 3 && mode != 4));
        assert((Asset_slot(Asset_get(), 0) != NULL) == (mode != 4));
        assert(Asset_slot(Asset_get(), 99) == NULL);
        if (mode == 2) { assert(reads[2] == 2); }
        if (mode == 3) { assert(reads[2] == 3); }
        if (mode == 4) { assert(reads[0] == 3 && Asset_splash()); }
        if (mode == 5) { assert(reads[1] == 3); }
    }
    free(pack);
    printf("PASS boot-to-SRAM mode %d, %u bytes\n", mode, len);
    return 0;
}
