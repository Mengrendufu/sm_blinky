//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef SM_FLASH_EVT_H_
#define SM_FLASH_EVT_H_

#include "sst.h"
#include "application_evt.h"
#include "flash_req.h"
#include "nvm.h"

//============================================================================
//=== Event contract shared by the Flash AO queue and the flash HSM
//
// Raw requests carry a pointer to the caller's request block, not a copy: the
// result has to land where the caller will look for it. The request is valid
// until completion notification. Its pooled envelope outlives dispatch and
// SST GC; no request access is allowed after notifying its caller.

typedef struct {
    SST_Evt super;
    Flash_Req *req;
} Flash_Evt;

// Pool-allocated event; SST GC reclaims it after dispatch.
// Payload selects the item; its RAM data is snapshotted at handling time.
typedef struct {
    SST_Evt super;
    Nvm_Id id;
} Flash_NvmEvt;

enum {
    FLASH_NULL_SIG = 0,
    // Raw NOR operations use physical byte addresses.
    FLASH_READ_SIG = APPLICATION_PUB_SIG_MAX,
    FLASH_WRITE_SIG,
    FLASH_ERASE_SIG,
    // Save one mapped application item; payload is Nvm_Id.
    FLASH_NVM_WRITE_SIG,
    LOADING_SPLASH_ASSETS_SIG,
    LOADING_ASSETS_SIG
};

#endif // SM_FLASH_EVT_H_
