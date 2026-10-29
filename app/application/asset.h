//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
//============================================================================
// Two-stage asset loading on the Flash AO task, using its bound DMA waits.
#ifndef ASSET_H_
#define ASSET_H_

#include <stdbool.h>
#include <stdint.h>

// Where the image sits in the NOR: the bootloader's layout, in the one place
// both the loader here and the run time read back agree on.
#include "nor_partitions.h"   /* the board NOR map */
#define ASSET_NOR_BASE    NOR_ASSET_BASE   /* from nor_partitions.h */

// One fixed-size slot per image; AST2 adds an individual CRC32 directory.
#define ASSET_SLOT_BYTES  NOR_ASSET_SLOT   /* from nor_partitions.h */

typedef struct {
    bool present;               // every declared image passed its own CRC
    uint32_t len;               // payload bytes (all slots together)
    uint32_t slots;             // how many whole slots the payload holds
    void const *pixels;         // RGB565, in the external SRAM
} Asset;

// Call once each, in order, on the Flash AO. Each image is loaded and
// CRC-checked with at most three attempts before publishing its pixels.
void Asset_loadSplash(void);
void Asset_loadRemaining(void);

// After the first phase-complete event: verified Splash pixels, or NULL.
// Its validity and pixels remain immutable throughout phase two.
void const *Asset_splash(void);

// Metadata is fixed before the first phase-complete event. The present
// result is read only after the second phase-complete event. Event delivery
// publishes the results; this module provides no runtime reload/locking.
Asset const *Asset_get(void);

// Reserved SRAM extent, including images that failed verification.
uint32_t Asset_bytes(void);

// After the second phase-complete event: verified content pixels, or NULL.
// Validity and pixels remain immutable after delivery.
void const *Asset_slot(Asset const *asset, uint32_t i);

#endif // ASSET_H_
