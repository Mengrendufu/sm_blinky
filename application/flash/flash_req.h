//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef FLASH_REQ_H_
#define FLASH_REQ_H_

#include <stdbool.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

//============================================================================
//=== Notification slots this module uses
//
// Slot 0 belongs to a task's own entry wake: the SST AO pend, the UI inbox,
// and the flash thread waiting for a request. Slot 1 is therefore where a
// flash RPC is completed, on the caller's own task - same convention at both
// ends, and no per-call object to create. The flash thread reads its own slot
// 1 the same way: its transfer's interrupt is what completes a chunk.
#define FLASH_REQ_INDEX  0U
#define FLASH_DONE_INDEX 1U

#if (configTASK_NOTIFICATION_ARRAY_ENTRIES < 2)
    #error "the flash RPC needs two notification slots per task"
#endif

//============================================================================
//=== One flash request
//
// This block is the whole handoff: the caller fills the intent in, the
// flash thread fills the result in, and the caller reads it after the wait.
// It lives on the caller's stack, so its lifetime needs no pool and no
// reference count - the caller is blocked until the thread is done with it.
//
// The completion is a notification on the caller's own slot, not a semaphore:
// one object less per call, and the same mechanism as the request wake at the
// other end. The call is synchronous, so a caller never has two requests in
// flight and one slot is always enough.

// dst is the caller's buffer either way: a destination for FLASH_OP_READ, a
// source for FLASH_OP_WRITE. Borrowed, never copied - the caller is blocked
// until the flash thread is done with it (see Flash_submit_ below).
typedef enum {
    FLASH_OP_PROBE = 0,
    FLASH_OP_READ,
    FLASH_OP_WRITE,
    FLASH_OP_ERASE
} Flash_Op;

typedef struct {
    Flash_Op op;
    uint32_t address;
    void *dst;
    uint32_t len;
    bool ok;                  // result: written by the flash thread
    TaskHandle_t waiter;      // caller to release when the work is done
    UBaseType_t waiterIndex;  // slot on that caller
} Flash_Req;

#endif // FLASH_REQ_H_
