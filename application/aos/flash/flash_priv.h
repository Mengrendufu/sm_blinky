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

// TOP init only: poll while mounting/restoring, then bind run-time waits.
void Flash_initStorage(void);

// One-shot checkpoint scheduling and storage actions, selected by the HSM.
void Flash_armCheckpoint(void);
void Flash_checkpoint(void);

// Complete a borrowed request and report AO stack usage. No request access
// is allowed after this call, because its caller may already have resumed.
void Flash_completeRequest(Flash_Req * const req);

// NVS access used by the HSM's request handlers, never by external clients.
bool Flash_kvRead(uint16_t id, void *dst, uint32_t len);
bool Flash_kvWrite(uint16_t id, void const *src, uint32_t len);

#endif // FLASH_PRIV_H_
