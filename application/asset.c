//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "asset.h"

#include "bsp.h"
#include "bsp_flash.h"
#include "crc32.h"

//============================================================================
// Where the bootloader puts it (see bootloader/bl_main.c and the charter's
// partition plan): a header sector, then the payload.
#define ASSET_BASE   ASSET_NOR_BASE
#define ASSET_DATA   (ASSET_BASE + 0x1000U)
#define ASSET_SRAM   0x68000000U    // external IS62WV51216, FSMC bank 3
#define ASSET_MAGIC  0x41535431U    // "AST1", the word the bootloader writes
#define ASSET_MAX    0x00080000U    // 512 KB: three fixed-size slots today
#define ASSET_TRACE  45U

static Asset Asset_;

//============================================================================
static uint32_t Asset_be32_(uint8_t const *p);
static uint32_t Asset_crc_(uint8_t const *data, uint32_t len);

//============================================================================
Asset Asset_load(void) {
    uint8_t hdr[16];
    uint32_t magic;
    uint32_t len;
    uint32_t crc;

    Asset_.present = false;
    Asset_.len = 0U;
    Asset_.crc32 = 0U;
    Asset_.slots = 0U;
    Asset_.pixels = (void const *)0;

    if (!BSP_flashRead(ASSET_BASE, hdr, sizeof hdr)) {
        return Asset_;
    }

    magic = Asset_be32_(&hdr[0]);
    len = Asset_be32_(&hdr[4]);
    crc = Asset_be32_(&hdr[8]);
    if ((magic != ASSET_MAGIC) || (len == 0U) || (len > ASSET_MAX)) {
        return Asset_;
    }

    if (!BSP_flashRead(ASSET_DATA, (uint8_t *)(uintptr_t)ASSET_SRAM, len)) {
        return Asset_;
    }

    // Over the SRAM copy, not over the bytes we just handed to the SPI:
    // what covers the whole path down to the buffer LVGL will read.
    Asset_.len = len;
    Asset_.crc32 = crc;
    Asset_.pixels = (void const *)ASSET_SRAM;
    Asset_.present = (Asset_crc_((uint8_t const *)(uintptr_t)ASSET_SRAM, len)
                      == crc);
    Asset_.slots = len / ASSET_SLOT_BYTES;

    SM_TRACE_BEGIN(ASSET_TRACE, 5)
        SM_TRACE_PAYLOAD_BE(Asset_.present ? 1U : 0U)
        SM_TRACE_PAYLOAD_BE((crc >> 24) & 0xFFU)
        SM_TRACE_PAYLOAD_BE((crc >> 16) & 0xFFU)
        SM_TRACE_PAYLOAD_BE((crc >> 8) & 0xFFU)
        SM_TRACE_PAYLOAD_BE(crc & 0xFFU)
    SM_TRACE_END()

    return Asset_;
}
//............................................................................
Asset const *Asset_get(void) {
    return &Asset_;
}
//............................................................................
uint32_t Asset_bytes(void) {
    return Asset_.present ? Asset_.len : 0U;
}
//............................................................................
void const *Asset_slot(Asset const *asset, uint32_t i) {
    if (!asset->present || (i >= asset->slots)) {
        return (void const *)0;
    }

    return (void const *)((uint8_t const *)asset->pixels
                          + (i * ASSET_SLOT_BYTES));
}
//............................................................................
static uint32_t Asset_be32_(uint8_t const *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
           | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
//............................................................................
// common_c's crc32 is the standard one - poly 0xEDB88320, init and xorout all
// ones, checked against zlib.crc32 on the host - but its one-shot call takes
// uint16_t length, so the payload goes through the byte-wise iterator.
static uint32_t Asset_crc_(uint8_t const *data, uint32_t len) {
    uint32_t crc = INIT_CRC32;
    uint32_t i;

    for (i = 0U; i < len; ++i) {
        crc = CRCIter_crc32(crc, data[i]);
    }

    return crc ^ XOROUT_CRC32;
}
