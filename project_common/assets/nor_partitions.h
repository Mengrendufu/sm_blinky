//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software; it comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
//============================================================================
//============================================================================
//=== How this board divides the part.
//
// Nothing here is a property of the flash: it is the firmware's allocation of
// it. Every user takes its window from this header rather than restating the
// numbers, and the assertions below are the whole point - an overlap that
// would only show up as corrupt assets or a wiped log is a build error here.

#ifndef NOR_PARTITIONS_H_
#define NOR_PARTITIONS_H_

#include "nor_w25q128.h"   // FLASH_DEV_SECTOR, FLASH_DEV_SIZE

/* Assets: three fixed-size slots, 153,600 B each (460,800 B in total). This
 * is what the slots occupy; the larger round number the asset code used to
 * declare was never the truth, and the NVS log starts close behind it. */
#define NOR_ASSET_BASE   0x00081000U
#define NOR_ASSET_SLOT   0x00025800U
#define NOR_ASSET_SLOTS  3U
#define NOR_ASSET_SIZE   (NOR_ASSET_SLOTS * NOR_ASSET_SLOT)
#define NOR_ASSET_DATA   (NOR_ASSET_BASE + FLASH_DEV_SECTOR)
#define NOR_ASSET_END    (NOR_ASSET_DATA + NOR_ASSET_SIZE)

/* The NVS log: 64 sectors, the region the port's primitives are bound to. */
#define NOR_NVS_BASE     0x00100000U
#define NOR_NVS_SECTORS  64U
#define NOR_NVS_SIZE     (NOR_NVS_SECTORS * FLASH_DEV_SECTOR)
#define NOR_NVS_END      (NOR_NVS_BASE + NOR_NVS_SIZE)

_Static_assert(NOR_ASSET_END <= NOR_NVS_BASE,
               "the asset window runs into the NVS log");
_Static_assert(NOR_NVS_END <= FLASH_DEV_SIZE,
               "the NVS log runs off the end of the part");

#endif /* NOR_PARTITIONS_H_ */
