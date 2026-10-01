//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef BSP_FLASH_H_
#define BSP_FLASH_H_

#include <stdbool.h>
#include <stdint.h>

//============================================================================
// W25Q128 (16 MB SPI NOR) on SPI2, from the core board IO allocation table
// (rows 73-76):
//   CS   = PB12 (F_CS)
//   SCK  = PB13
//   MISO = PB14
//   MOSI = PB15
// SPI1 is not an option here: on this board it belongs to the VS1053 socket.
// The wireless socket shares SPI2 and gives up its own chip select.
//
// The bus setup is part of board bring-up. The verification is a separate
// call on purpose: bring-up happens long before a terminal can be attached,
// and the CH340 auto-download branch can hold the chip in reset right then
// (see AGENTS.md), so the flash thread runs the probe late instead.

void BSP_flashInit(void);

// One JEDEC ID read plus six bytes from address 0, all reported on the trace
// channel. The raw bytes go out, so a device that answers with something
// unexpected is still readable on the wire.
void BSP_flashProbe(void);

// Block read (03h). One transaction: chip select stays low from the
// command byte through the last data byte, and the device walks the
// address itself, so len has no page boundary to respect - only the array
// size.
//
// Byte wide for now (one HAL call per byte, the bottleneck the DMA path
// removes later). Whoever calls this owns SPI2 for the duration: at run time
// that is the flash thread, and nothing else may touch the bus meanwhile.
bool BSP_flashRead(uint32_t address, uint8_t *dst, uint32_t len);

// Read/write self test: it erases every sector, writes a signature and reads
// it back. Start it once - it spins up its own task, because a multi minute
// sweep has no business running in an AO whose event queue holds 8 entries.
void BSP_flashSelfTestStart(void);

#endif // BSP_FLASH_H_
