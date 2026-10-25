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
#include "bsp_sram.h"
#include "crc32.h"
#include "FreeRTOS.h"
#include "task.h"

//============================================================================
#define ASSET_BASE   ASSET_NOR_BASE
#define ASSET_DATA   (ASSET_BASE + 0x1000U)
#define ASSET_SRAM   BSP_SRAM_BASE
#define ASSET_MAGIC  0x41535431U
#define ASSET_MAX    NOR_ASSET_SIZE
#define ASSET_TRACE  45U
#define ASSET_SPLASH_SLOT 2U

static Asset Asset_;
static bool Asset_splashReady_;

static void Asset_dbgBootReport_(void);
static uint32_t Asset_be32_(uint8_t const *p);
static uint32_t Asset_crc_(uint8_t const *data, uint32_t len);

//============================================================================
// Flash AO only. Device operations and waits have already been bound by the
// owner. Metadata is fixed before the first UI readiness event is posted.
void Asset_loadSplash(void) {
    uint8_t hdr[16];
    uint32_t len;

    Asset_ = (Asset){0};
    Asset_splashReady_ = false;
    if (!BSP_flashRead(BSP_flashDevice(), ASSET_BASE, hdr, sizeof hdr)) {
        return;
    }
    len = Asset_be32_(&hdr[4]);
    if ((Asset_be32_(hdr) != ASSET_MAGIC)
        || (len == 0U) || (len > ASSET_MAX)) {
        return;
    }
    Asset_.len = len;
    Asset_.crc32 = Asset_be32_(&hdr[8]);
    Asset_.slots = len / ASSET_SLOT_BYTES;
    Asset_.pixels = (void const *)ASSET_SRAM;

    if (Asset_.slots > ASSET_SPLASH_SLOT) {
        uint32_t const offset = ASSET_SPLASH_SLOT * ASSET_SLOT_BYTES;
        bool const ok = BSP_flashRead(BSP_flashDevice(),
            ASSET_DATA + offset,
            (void *)(uintptr_t)(ASSET_SRAM + offset), ASSET_SLOT_BYTES);

        // The legacy header only carries a whole-payload CRC. Splash is
        // provisional until phase two validates all slots.
        taskENTER_CRITICAL();
        Asset_splashReady_ = ok;
        taskEXIT_CRITICAL();
    }
}

void Asset_loadRemaining(void) {
    if (Asset_.len != 0U) {
        // A published splash is never overwritten while LVGL borrows it.
        // If no splash was published, read the complete payload instead.
        uint32_t const n = Asset_splashReady_
            ? ASSET_SPLASH_SLOT * ASSET_SLOT_BYTES : Asset_.len;
        bool const read = BSP_flashRead(BSP_flashDevice(), ASSET_DATA,
            (void *)(uintptr_t)ASSET_SRAM, n);
        bool const valid = read
            && (Asset_crc_((uint8_t const *)(uintptr_t)ASSET_SRAM,
                            Asset_.len) == Asset_.crc32);

        taskENTER_CRITICAL();
        Asset_.present = valid;
        Asset_splashReady_ = Asset_splashReady_ && valid;
        taskEXIT_CRITICAL();
    }
    Asset_dbgBootReport_();
}

// Report once, on the Flash task after final validation.
static void Asset_dbgBootReport_(void) {
    SM_TRACE_BEGIN(ASSET_TRACE, 5)
        SM_TRACE_PAYLOAD_BE(Asset_.present ? 1U : 0U)
        SM_TRACE_PAYLOAD_BE((Asset_.crc32 >> 24) & 0xFFU)
        SM_TRACE_PAYLOAD_BE((Asset_.crc32 >> 16) & 0xFFU)
        SM_TRACE_PAYLOAD_BE((Asset_.crc32 >> 8) & 0xFFU)
        SM_TRACE_PAYLOAD_BE(Asset_.crc32 & 0xFFU)
    SM_TRACE_END()
}

Asset const *Asset_get(void) {
    return &Asset_;
}

uint32_t Asset_bytes(void) {
    // The address range stays reserved even if final CRC validation fails.
    return Asset_.len;
}

void const *Asset_splash(void) {
    void const *pixels;

    taskENTER_CRITICAL();
    pixels = Asset_splashReady_
        ? (void const *)(uintptr_t)(ASSET_SRAM
            + ASSET_SPLASH_SLOT * ASSET_SLOT_BYTES) : (void const *)0;
    taskEXIT_CRITICAL();
    return pixels;
}

void const *Asset_slot(Asset const *asset, uint32_t i) {
    void const *pixels = (void const *)0;

    taskENTER_CRITICAL();
    if (asset->present && (i < asset->slots)) {
        pixels = (uint8_t const *)asset->pixels + i * ASSET_SLOT_BYTES;
    }
    taskEXIT_CRITICAL();
    return pixels;
}

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
