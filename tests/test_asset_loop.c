// Host integration: production boot protocol + loader, mocked UART/NOR.
// Run through test_asset_loop.py; it generates script-produced packages.
#define _GNU_SOURCE
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define main Bl_unusedMain
#include "../bootloader/bl_main.c"
#undef main
#include "../application/asset.c"

uint32_t _snoinit;
BSP_flashOperations const BSP_flashDmaOperations = {0};
static BSP_Flash device;
static uint8_t nor[0x140000];
static uint8_t stream[ASSET_FORMAT_MAX_PACKAGE + 8192];
static uint32_t inputLen, inputPos, ackCount, nakCount, writes, erases;
static uint32_t reads[3], headerReads;
static int faultSlot = -1, faultCount;
static bool failRead;
static jmp_buf stopped;

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
uint32_t HAL_GetTick(void) { return 0; }
void HAL_Delay(uint32_t ms) { (void)ms; }
_Noreturn void NVIC_SystemReset(void) { longjmp(stopped, 2); }
int HAL_UART_Receive(UART_HandleTypeDef *uart, uint8_t *data,
                     uint16_t len, uint32_t timeout) {
    (void)uart; (void)timeout;
    if (inputPos + len > inputLen) { longjmp(stopped, 1); }
    memcpy(data, stream + inputPos, len);
    inputPos += len;
    return HAL_OK;
}
int HAL_UART_Transmit(UART_HandleTypeDef *uart, uint8_t *data,
                      uint16_t len, uint32_t timeout) {
    (void)uart; (void)timeout;
    if (len == 1 && data[0] == BL_ACK_MAGIC) { ++ackCount; }
    if (len == 1 && data[0] == BL_NAK_MAGIC) { ++nakCount; }
    return HAL_OK;
}

static void append(void const *bytes, uint32_t len) {
    assert(inputLen + len <= sizeof stream);
    memcpy(stream + inputLen, bytes, len);
    inputLen += len;
}
static void handshake(uint32_t len) {
    uint8_t hdr[10] = {BL_REQ_MAGIC, BL_PROTO_VER};
    Bl_putBe32_(hdr + 2, len);
    Bl_putBe32_(hdr + 6, Bl_crc32_(hdr + 1, 5));
    append(hdr, sizeof hdr);
}
static void block(uint8_t const *pack, uint32_t index, uint32_t len) {
    uint8_t hdr[7] = {BL_BLK_MAGIC, index >> 8, index,
                     len >> 8, len, 0, 0};
    uint32_t const crc = Bl_crc32_(pack, len);
    hdr[5] = crc >> 8; hdr[6] = crc;
    append(hdr, sizeof hdr);
    append(pack, len);
}
static void finish(uint8_t const *pack, uint32_t len) {
    uint8_t hdr[9] = {BL_FIN_MAGIC};
    Bl_putBe32_(hdr + 1, len);
    Bl_putBe32_(hdr + 5, Bl_crc32_(pack, len));
    append(hdr, sizeof hdr);
}

int main(int argc, char **argv) {
    assert(argc == 3);
    FILE *file = fopen(argv[1], "rb");
    assert(file);
    uint8_t *pack = malloc(ASSET_FORMAT_MAX_PACKAGE);
    assert(pack);
    uint32_t const len = fread(pack, 1, ASSET_FORMAT_MAX_PACKAGE, file);
    fclose(file);
    assert(AssetFormat_valid(pack));
    int const mode = atoi(argv[2]);
    memset(nor, 0xFF, sizeof nor);
    memset(nor + NOR_NVS_BASE, 0xA5, NOR_NVS_SIZE);
    assert(mmap((void *)(uintptr_t)ASSET_SRAM, 0x100000,
                PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS
                | MAP_FIXED_NOREPLACE, -1, 0) != MAP_FAILED);
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
            finish(pack, len);
            if (mode == 14) { stream[inputLen - 1] ^= 1; }
        }
    }
    int const result = setjmp(stopped);
    if (result == 0) { Bl_burn_(); }
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
