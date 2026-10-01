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
//=== Inbox internals -- private to application/flash/thread

#ifndef FLASH_REQ_PRIV_H_
#define FLASH_REQ_PRIV_H_

#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "flash_req.h"

// flash.c binds the thread once it exists; a submit from then on also
// wakes it.
void Flash_reqBind(TaskHandle_t thread);

// Flash thread only: one consumer, so the pop side needs no coordination
// beyond the critical section it shares with the producers.
bool Flash_reqPop(Flash_Req ** const out);

// Flash thread only: hand the finished request back to its caller.
void Flash_reqFinish(Flash_Req * const req);

#endif // FLASH_REQ_PRIV_H_
