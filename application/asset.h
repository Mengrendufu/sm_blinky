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

// One slot per background. The images are all the same size today, so the
// payload is just slots laid end to end; a table with per-slot lengths is the
// step after this one (it only earns its keep once they differ).
#define ASSET_SLOT_BYTES  NOR_ASSET_SLOT   /* from nor_partitions.h */

typedef struct {
    bool present;               // header sane and the payload CRC matched
    uint32_t len;               // payload bytes (all slots together)
    uint32_t crc32;             // payload CRC32, as the header wrote it
    uint32_t slots;             // how many whole slots the payload holds
    void const *pixels;         // RGB565, in the external SRAM
} Asset;

// Call once each, in order, on the Flash AO. Splash is provisional until
// loadRemaining validates the whole payload. Its SRAM region stays stable.
void Asset_loadSplash(void);
void Asset_loadRemaining(void);

// Provisional splash pixels, or NULL. Returns NULL after final CRC failure;
// the UI detaches any borrowed descriptor when notified of completion.
void const *Asset_splash(void);

// Metadata is fixed before the first notification; present is published
// under a critical section and is consumed through Asset_slot.
Asset const *Asset_get(void);

// Reserved SRAM extent, including provisional or subsequently invalid data.
uint32_t Asset_bytes(void);

// Only verified content-page pixels are exposed through this accessor.
void const *Asset_slot(Asset const *asset, uint32_t i);

#endif // ASSET_H_
