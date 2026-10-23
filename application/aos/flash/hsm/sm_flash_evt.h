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
#include "flash_req.h"

//============================================================================
//=== Event contract shared by the Flash AO queue and the flash HSM
//
// The event carries a pointer to the caller's request block, not a copy: the
// result has to land where the caller will look for it. The request is valid
// until completion notification. Its static envelope outlives dispatch and
// SST GC; no request access is allowed after notifying its caller.

typedef struct {
    SST_Evt super;
    Flash_Req *req;
} Flash_Evt;

enum {
    FLASH_NULL_SIG = 0,
    FLASH_REQ_SIG,
    FLASH_CHECKPOINT_SIG
};

#endif // SM_FLASH_EVT_H_
