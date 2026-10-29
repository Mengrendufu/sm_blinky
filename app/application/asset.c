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
#include "asset_format.h"

#include "bsp.h"
#include "bsp_flash.h"
#include "bsp_sram.h"
#include "crc32.h"

//============================================================================
#define ASSET_BASE   ASSET_NOR_BASE
#define ASSET_DATA   NOR_ASSET_DATA
#define ASSET_SRAM   BSP_SRAM_BASE
#define ASSET_TRACE  45U
#define ASSET_SPLASH_SLOT 2U
#define ASSET_LOAD_ATTEMPTS 3U

_Static_assert(ASSET_SLOT_BYTES == ASSET_FORMAT_SLOT_BYTES,
               "asset format and NOR slots disagree");
_Static_assert(NOR_ASSET_SLOTS == ASSET_FORMAT_SLOTS,
               "asset format and NOR count disagree");

static Asset Asset_;
static uint8_t Asset_header_[ASSET_FORMAT_HEADER];
static bool Asset_valid_[NOR_ASSET_SLOTS];
static uint32_t Asset_crc32_[NOR_ASSET_SLOTS];

static void Asset_dbgBootReport_(void);
static uint32_t Asset_crc_(uint8_t const *data, uint32_t len);

// Only unpublished regions may be written. The Flash AO owns loading;
// successful images stay immutable while UI borrows their SRAM pixels.
static bool Asset_loadSlot_(uint32_t slot) {
    uint32_t const offset = slot * ASSET_SLOT_BYTES;
    uint8_t * const dst = (uint8_t *)(uintptr_t)(ASSET_SRAM + offset);

    for (uint32_t attempt = 0U; attempt < ASSET_LOAD_ATTEMPTS; ++attempt) {
        if (BSP_flashRead(BSP_flashDevice(), ASSET_DATA + offset,
                          dst, ASSET_SLOT_BYTES)
            && (Asset_crc_(dst, ASSET_SLOT_BYTES) == Asset_crc32_[slot])) {
            Asset_valid_[slot] = true;
            return true;
        }
    }
    return false;
}

// Flash AO only, after binding its DMA operations and yielding waits.
void Asset_loadSplash(void) {
    Asset_ = (Asset){0};
    for (uint32_t i = 0U; i < NOR_ASSET_SLOTS; ++i) {
        Asset_valid_[i] = false;
    }
    bool valid = false;
    for (uint32_t attempt = 0U; attempt < ASSET_LOAD_ATTEMPTS; ++attempt) {
        if (BSP_flashRead(BSP_flashDevice(), ASSET_BASE, Asset_header_,
                          sizeof Asset_header_)
            && AssetFormat_valid(Asset_header_)) {
            valid = true;
            break;
        }
    }
    if (!valid) {
        return;
    }
    Asset_.len = AssetFormat_be32(&Asset_header_[12]);
    Asset_.slots = AssetFormat_be32(&Asset_header_[8]);
    Asset_.pixels = (void const *)ASSET_SRAM;
    for (uint32_t i = 0U; i < Asset_.slots; ++i) {
        Asset_crc32_[i] = AssetFormat_be32(
            &AssetFormat_entry(Asset_header_, i)[12]);
    }
    if (Asset_.slots > ASSET_SPLASH_SLOT) {
        (void)Asset_loadSlot_(ASSET_SPLASH_SLOT);
    }
}

void Asset_loadRemaining(void) {
    bool valid = (Asset_.slots != 0U);

    for (uint32_t i = 0U; i < Asset_.slots; ++i) {
        // Splash was already attempted in stage one. Never overwrite a
        // published image, nor silently retry a failed phase later.
        if (i != ASSET_SPLASH_SLOT) {
            (void)Asset_loadSlot_(i);
        }
        valid = Asset_valid_[i] && valid;
    }
    Asset_.present = valid;
    Asset_dbgBootReport_();
}

// Report once, on the Flash task after final validation.
static void Asset_dbgBootReport_(void) {
    uint32_t mask = 0U;
    for (uint32_t i = 0U; i < Asset_.slots; ++i) {
        mask |= Asset_valid_[i] ? (1U << i) : 0U;
    }
    SM_TRACE_BEGIN(ASSET_TRACE, 5)
        SM_TRACE_PAYLOAD_BE(Asset_.present ? 1U : 0U)
        SM_TRACE_PAYLOAD_BE((mask >> 24) & 0xFFU)
        SM_TRACE_PAYLOAD_BE((mask >> 16) & 0xFFU)
        SM_TRACE_PAYLOAD_BE((mask >> 8) & 0xFFU)
        SM_TRACE_PAYLOAD_BE(mask & 0xFFU)
    SM_TRACE_END()
}

Asset const *Asset_get(void) {
    return &Asset_;
}

uint32_t Asset_bytes(void) {
    // The address range stays reserved even if individual images fail CRC.
    return Asset_.len;
}

// Phase-complete events publish these values. Consumers do not read a
// phase's results while the Flash AO is still loading that phase.
void const *Asset_splash(void) {
    return Asset_valid_[ASSET_SPLASH_SLOT]
        ? (void const *)(uintptr_t)(ASSET_SRAM
            + ASSET_SPLASH_SLOT * ASSET_SLOT_BYTES) : (void const *)0;
}

void const *Asset_slot(Asset const *asset, uint32_t i) {
    if ((i < asset->slots) && Asset_valid_[i]) {
        return (uint8_t const *)asset->pixels + i * ASSET_SLOT_BYTES;
    }
    return (void const *)0;
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
