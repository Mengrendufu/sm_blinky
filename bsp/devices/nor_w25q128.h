//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software; it comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
//============================================================================
//============================================================================
//=== The part itself, as its datasheet states it.
//
// No code, no dependencies, nothing to link: the BSP driver and the NVS port
// both include this, and neither should have to pull in the other's header to
// learn how big a page is. Geometry only: command opcodes and timings are
// the driver's business and stay there.

#ifndef NOR_W25Q128_H_
#define NOR_W25Q128_H_

#define FLASH_DEV_PAGE    256UL     /* program unit */
#define FLASH_DEV_SECTOR  4096UL    /* erasable unit */
#define FLASH_DEV_SECTORS 4096U     /* 16 MB / 4 KB */
#define FLASH_DEV_SIZE    (FLASH_DEV_SECTOR * FLASH_DEV_SECTORS)
#define FLASH_DEV_ERASED  0xFFU     /* what an erased byte reads back as */

#endif /* NOR_W25Q128_H_ */
