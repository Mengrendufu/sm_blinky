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

// Block read (03h). One transaction: chip select stays low from the
// command byte through the last data byte, and the device walks the
// address itself, so len has no page boundary to respect - only the array
// size.
//
// Block wise: the command and address stay byte at a time, the data goes out
// through HAL_SPI_Receive() in blocks of FLASH_BLOCK_MAX. Whoever calls this
// owns SPI2 for the duration: at run time that is the flash thread, and
// nothing else may touch the bus meanwhile.
bool BSP_flashRead(uint32_t address, uint8_t *dst, uint32_t len);

// The same read moved by DMA, waited out by polling a flag instead of a task
// notification: the boot path (Asset_load) has no task to notify, but it does
// have interrupts now. Any length; it splits into FLASH_DMA_CHUNK pieces
// itself. Blocking, so it is for the boot path only.
bool BSP_flashReadDmaWait(uint32_t address, uint8_t *dst, uint32_t len);

// Erase one 4 KB sector. `address` must be sector aligned (a misaligned one
// would erase the sector around it), and this form spins on the device until
// it is done - blockingly, for the bootloader and the self test. Measured
// ~130 to 160 ms on this part.
bool BSP_flashEraseSector(uint32_t address);

// The same erase without the wait: it starts the erase, and the caller polls
// BSP_flashIsReady(). A run time caller uses this one, because the erase is
// the device's time and a yielding wait costs no CPU at all.
bool BSP_flashEraseSectorStart(uint32_t address);

// Program up to one 256 B page: `address` must be page aligned and the range
// must stay inside that page. Waits the device out before returning.
bool BSP_flashProgramPage(uint32_t address, uint8_t const *data,
                          uint32_t len);

// Read/write self test: it erases every sector, writes a signature and reads
// it back. Start it once - it spins up its own task, because a multi minute
// sweep has no business running in an AO whose event queue holds 8 entries.
// (the write path self test that used to live here was removed 2026-10-10:
// it drove SPI2 from a second context; see the note in bsp_flash.c)

// Run time read, moved by DMA and finished in its interrupt: the caller is
// not held on the bus, it is called back when the data has landed. The boot
// path (BSP_flashRead above) stays the blocking one, because before the
// scheduler interrupts are off and nothing interrupt driven can complete.
//
// One read at a time, and at most FLASH_DMA_CHUNK bytes: a longer read is the
// caller's loop over chunks (which is where a yield between them belongs). A
// second call while one is in flight, or an oversized one, is refused with
// done(false). The callback runs in interrupt context - keep it short, and do
// not call anything that blocks, and do not start another read from it.
typedef void (*BSP_flashDoneCb)(bool ok);

// The NOR program page: a write may be 1 to this many bytes, and it must not
// cross one of these boundaries (the device wraps and reprograms the start of
// the page). The page itself, datasheet 7.2.x.
#define FLASH_DEV_PAGE 256UL

// One arm moves this much and no more. 32 KB is ~15 ms on the wire, which is
// the window the caller gets between chunks; the size is here because the
// caller is the one that loops.
#define FLASH_DMA_CHUNK 32768UL

void BSP_flashReadDma(uint32_t address, uint8_t *dst, uint32_t len,
                      BSP_flashDoneCb done);

// One page at most, moved by DMA, for the run time writer. Same guard as the
// blocking BSP_flashProgramPage (page aligned, never across a boundary), same
// one-transaction-per-arm rule, same callback context. The device is still
// programming when the callback fires, so the caller waits for
// BSP_flashIsReady() afterwards - that wait is a spin here, and the caller is
// the one that gets to block instead of spinning.
void BSP_flashProgramPageDma(uint32_t address, uint8_t const *data,
                             uint32_t len, BSP_flashDoneCb done);

// One poll of the device's status register, true when it is done with its own
// work (page program, sector erase). Nothing else: the waiting policy lives
// above this layer, on purpose - see the note next to the implementation.
bool BSP_flashIsReady(void);

bool BSP_flashDmaBusy(void);
bool BSP_flashDmaFailed(void);

#endif // BSP_FLASH_H_
