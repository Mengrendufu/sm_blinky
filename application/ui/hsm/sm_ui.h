//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef SM_UI_H_
#define SM_UI_H_

#include "sm_hsm.h"
#include "sm_ui_evt.h"

//============================================================================
//=== UI state machine
//
// One instance, owned by the UI thread and dispatched synchronously from its
// inbox drain -- never through an SST queue, so the UI thread is the only one
// that touches it.
// Ui_running is the only state for now; screen modes (status page, menu) grow
// off it later.

typedef struct {
    SM_Hsm sm_hsm_;
    bool ledOn;          // last LED state posted by the Blinky AO
    uint16_t ledEdges;   // LED transitions seen; the status page seed
} Sm_Ui;

void Sm_Ui_init(Sm_Ui * const me);
void Sm_Ui_dispatch(Sm_Ui * const me, Ui_Evt const * const e);

#endif // SM_UI_H_
