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

#include <stdint.h>
#include "flash_req.h"

//============================================================================
//=== Event contract shared by the flash inbox and the flash HSM
//
// The event carries a pointer to the caller's request block, not a copy: the
// result has to land where the caller will look for it. The block stays valid
// for the whole dispatch because the caller is blocked until the thread
// releases it.

typedef uint16_t Flash_Sig;

typedef struct {
    Flash_Sig sig;
    Flash_Req *req;
} Flash_Evt;

enum {
    FLASH_NULL_SIG = 0,
    FLASH_REQ_SIG
};

#endif // SM_FLASH_EVT_H_
