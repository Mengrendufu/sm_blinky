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
// One instance, owned by the flash thread and dispatched synchronously from
// its inbox drain, so the thread is the only one that touches it.
//
// The handlers do the transfer inline. Waiting inside a handler is deliberate
// here: this thread has no event queue behind it, so a slow handler delays
// the next request and nothing else - and a wait costs no CPU, where the
// blocking read it replaced spun for the whole transfer and starved the idle
// task that drains the tracer. Flash_ready is the only state for now; the
// maintenance and upgrade modes grow off it later.

typedef struct {
    SM_Hsm sm_hsm_;
} Sm_Flash;

void Sm_Flash_init(Sm_Flash * const me);
void Sm_Flash_dispatch(Sm_Flash * const me, Flash_Evt const * const e);

#endif // SM_FLASH_H_
