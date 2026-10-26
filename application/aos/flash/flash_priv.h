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
//=== Flash HSM actions -- internal to application/aos/flash

#ifndef FLASH_PRIV_H_
#define FLASH_PRIV_H_

#include "flash_req.h"

// First bring-up event binds the DMA operations and AO-task waits.
void Flash_bindRuntime(void);

// Ready entry: restore NVS, then expose synchronous RPC ingress.
void Flash_initStorage(void);

// Save one mapped RAM item in response to an application event.
void Flash_writeNvm(Nvm_Id id);

// Complete a borrowed request and report AO stack usage. No request access
// is allowed after this call, because its caller may already have resumed.
void Flash_completeRequest(Flash_Req * const req);

#endif // FLASH_PRIV_H_
