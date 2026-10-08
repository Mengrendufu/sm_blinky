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
//=== The asset the bootloader wrote into the NOR
//
// One image, read once. The bootloader's 16-byte header sits at the base
// and the payload in the sector after it; this module copies the payload into
// the external SRAM and checks that copy against the CRC the header carries.
// The pixels the UI gets are therefore verified end to end: the bootloader
// checked them when it wrote them (read-back per block), and this checks the
// path they take back (NOR -> SPI -> FSMC -> the buffer LVGL reads).
//
// Called from main() before the first FreeRTOS call: BSP_flashRead() polls
// SPI itself and needs no scheduler, and from that first timer call the
// critical-section nesting is already odd (see the comment in main.c).
//============================================================================
#ifndef ASSET_H_
#define ASSET_H_

#include <stdbool.h>
#include <stdint.h>

// One slot per background. The images are all the same size today, so the
// payload is just slots laid end to end; a table with per-slot lengths is the
// step after this one (it only earns its keep once they differ).
#define ASSET_SLOT_BYTES  153600U   // 240 x 320 x 2, RGB565

typedef struct {
    bool present;               // header sane and the payload CRC matched
    uint32_t len;               // payload bytes (all slots together)
    uint32_t crc32;             // payload CRC32, as the header wrote it
    uint32_t slots;             // how many whole slots the payload holds
    void const *pixels;         // RGB565, in the external SRAM
} Asset;

// Read and verify the asset. Call once, from main().
Asset Asset_load(void);

// What Asset_load() found. Never NULL.
Asset const *Asset_get(void);

// How many bytes of the external SRAM the asset occupies (0 when absent).
// Anything that writes into that SRAM has to stay out of this range.
uint32_t Asset_bytes(void);

// The pixels of slot i, or NULL when the asset is not present or i is past
// last slot.
void const *Asset_slot(Asset const *asset, uint32_t i);

#endif // ASSET_H_
