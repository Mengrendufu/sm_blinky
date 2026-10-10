//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef FLASH_H_
#define FLASH_H_

#include <stdbool.h>
#include <stdint.h>

//============================================================================
//=== Flash thread -- the owner of the SPI flash at run time
//
// A plain FreeRTOS task with an HSM inside, not an SST AO: a sector erase is
// 130 ms and a whole chip sweep is minutes, and an AO whose handler runs that
// long fills its 8 slot queue and trips DBC_REQUIRE(300). This thread has no
// event queue and no beat - it sleeps until a request arrives.
//
// SPI2 is one indivisible resource: two contexts on it would interleave
// command, address and data of different transactions. So every other thread
// reaches the device through these calls, which post a request, hand the CPU
// away and wake up when the work is done. The wait is a real block, not a
// spin.
//
// Buffer: borrowed for the duration of the call. The caller hands the address
// over, must not touch it until the call returns, and gets it back filled.
// Nothing is allocated - the request block lives on the caller's stack, which
// the blocking wait keeps alive, and the completion is a notification on the
// caller's own slot.
//
// Call these from a task, once the scheduler is running: the wait is an RTOS
// block and a request posted before that could not be served.
//
// A caller that is an SST AO has to keep the block short: its queue is 8
// deep and the beat keeps posting into it while the handler waits. The flash
// thread itself has no such queue, which is why the long work lives there.

// Called from main() before the scheduler starts.
void Flash_start(void);

// JEDEC identity plus six bytes from address 0, both reported on the trace
// channel. Called late (the AO asks for it 30 s in) so a terminal is attached
// by then.

// Block read of len bytes from address into dst. Blocks the caller until the
// data is in dst.
bool Flash_readBlock(uint32_t address, void *dst, uint32_t len);

// Write len bytes from src, any length and any address: the flash thread
// splits the range at page boundaries and waits the device out between
// pages. The device's 0.7 to 3 ms per page and 130 to 160 ms per sector are
// slept through, not spun - the CPU belongs to whatever else has work.
bool Flash_writeBlock(uint32_t address, void const *src, uint32_t len);

// Erase one 4 KB sector: what a write needs first. Same sleeping wait.
bool Flash_eraseSector(uint32_t address);

#endif // FLASH_H_
