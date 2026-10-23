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

// Device initialization shared by all phases.
void BSP_flashInit(void);

//============================================================================
// Bound wait API -- bootloader asset burning and the run-time flash task.
// Bind their respective wait capabilities before issuing reads/writes/erases.

// Waiting capabilities required by the BSP, implemented by the caller.
// context is opaque to the BSP and must outlive every transfer. isCaller
// accepts only the owning execution context, never an ISR. waitDone consumes
// a completion notification (including one posted before waiting); false is
// timeout. notifyDone must work both in an ISR and on inline start failure.
// sleepMs delays execution (busy delay or yielding). Times are milliseconds.
typedef struct {
    void *context;
    bool (*isCaller)(void *context);
    bool (*waitDone)(void *context, uint32_t timeoutMs);
    void (*notifyDone)(void *context);
    void (*sleepMs)(void *context, uint32_t delayMs);
} BSP_flashWaitOps;

// Bind once from the owner before first use. Copies the structure; no queue
// or mutex is provided. Only the caller accepted by isCaller may use waits.
void BSP_flashBindWait(BSP_flashWaitOps const *ops);

// Synchronous completion; DMA data transfer waits use the bound capability.
// Buffers are borrowed until return. Writes split at page boundaries and
// use sleepMs while the device programs. Destination must be erased.
// False reports transfer/device failure. Lost DMA completion is fail-stop:
// returning then could release a buffer still owned by the DMA.
bool BSP_flashReadWait(uint32_t address, void *dst, uint32_t len);
bool BSP_flashWriteWait(uint32_t address, void const *src, uint32_t len);

// One aligned 4 KB sector, using sleepMs between BUSY polls. False reports a
// start failure or device timeout; it does not cancel an ongoing erase.
bool BSP_flashEraseSectorWait(uint32_t address);

//============================================================================
// Startup asset loading only -- Asset_load(), before the scheduler starts.
// These reads do not use BSP_flashWaitOps and never yield the CPU.
// Bootloader burning and run-time clients use the bound wait API above.

// Block read (03h). One transaction: chip select stays low from the
// command byte through the last data byte, and the device walks the
// address itself, so len has no page boundary to respect - only the array
// size.
//
// Boot-only polling read, before the run-time owner starts using the bus.
// No wait capability binding is needed; the CPU polls the SPI throughout.
bool BSP_flashRead(uint32_t address, uint8_t *dst, uint32_t len);

// The same read moved by DMA, waited out by polling a flag instead of a task
// notification: the boot path (Asset_load) has no task to notify, but it does
// have interrupts now. Any length; the driver splits the transfer internally.
// Blocking, so it is for the boot path only.
bool BSP_flashReadDmaWait(uint32_t address, uint8_t *dst, uint32_t len);

#endif // BSP_FLASH_H_
