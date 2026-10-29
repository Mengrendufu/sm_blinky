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
#include "sst.h"
#include "nvm.h"

#define FLASH_QUEUE_LEN 10U

//============================================================================
//=== Flash AO -- the owner of the SPI flash at run time
//
// A dedicated SST AO. Events run to completion in FIFO order; DMA waits
// and device BUSY polling yield the CPU inside the current handler without
// dispatching another event. Slot 0 wakes SST; slot 1 completes DMA.
// NVM saves run only in response to application-posted item events.
//
// SPI2 is one indivisible resource: two contexts on it would interleave
// command, address and data of different transactions. So every other thread
// reaches the device through these calls, which post a request, hand the CPU
// away and wake up when the work is done. The wait is a real block, not a
// spin.
//
// Buffer: borrowed for the duration of the call. The caller hands the address
// over, must not touch it until the call returns, and gets it back filled.
// The event envelope comes from SST; the request stays on the caller's stack,
// kept alive by the blocking wait. Completion is a notification on the
// caller's own slot.
//
// Call from a task after Flash has entered ready and restored NVS. Ingress
// is bound only then; a request before binding violates the contract.
//
// Up to eight accepted raw RPCs, including the one executing. Further calls
// return false without borrowing the buffer. Callers must not call these
// synchronous RPCs from the Flash AO itself. Other AOs must account for any
// events queued on them while their handler waits for this RPC.

// Constructed and started by AO_start(), before the scheduler starts.
extern SST_Task * const AO_Flash;
void Flash_ctor(void);

//============================================================================
// Raw NOR requests -- physical byte addresses, bypassing NVS.
// The caller owns the target address range; no key ID interpretation here.

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

//============================================================================
// Pool-backed NVM event ingress: operation signal + logical item ID.
// Currently supports FLASH_NVM_WRITE_SIG from sm_flash_evt.h.
// Nonblocking, task context. False means storage initialization is pending.
// Every accepted call queues a separate event; no per-item coalescing.
// Queue/pool capacity must cover pending requests, following SST contracts.
// Saves sample RAM when handled; Nvm_writeResult reports completed IO.
bool Flash_postNvm(SST_Signal sig, Nvm_Id id);

#endif // FLASH_H_
