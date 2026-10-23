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
//=== AO request adapter -- private to application/aos/flash

#ifndef FLASH_REQ_PRIV_H_
#define FLASH_REQ_PRIV_H_

#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "sm_flash_evt.h"
#include "flash.h"

#define FLASH_REQUEST_CAPACITY (FLASH_QUEUE_LEN - 1U)

// Bind once after the AO queue exists, before any request is submitted.
void Flash_reqBind(SST_Task *ao);

// Flash thread only: hand the finished request back to its caller.
void Flash_reqFinish(Flash_Req * const req);

#endif // FLASH_REQ_PRIV_H_
