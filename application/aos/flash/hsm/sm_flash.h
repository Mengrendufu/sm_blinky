//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef SM_FLASH_H_
#define SM_FLASH_H_

#include "sm_hsm.h"
#include "sm_flash_evt.h"

//============================================================================
//=== Flash state machine
//
// One instance, owned by the Flash AO and dispatched one event at a time.
// Handlers may yield while waiting for DMA or device readiness. The current
// event runs to completion before the AO consumes the next queued event.

typedef struct {
    SST_Task super;
    SM_Hsm sm_hsm_;
} Sm_Flash;

// SST virtual functions, registered directly. init runs on the starter;
// dispatch runs on the AO task.
void Sm_Flash_init(SST_Task * const me, SST_Evt const * const e);
void Sm_Flash_dispatch(SST_Task * const me, SST_Evt const * const e);

#endif // SM_FLASH_H_
