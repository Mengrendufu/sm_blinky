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
#include "FreeRTOS.h"   // TickType_t: the splash hold is measured in ticks

//============================================================================
//=== UI state machine
//
// One instance, owned by the UI thread and dispatched synchronously from its
// inbox drain -- never through an SST queue, so the UI thread is the only one
// that touches it.
//
// The machine owns the screen in pages: Splash first (the boot image, pushed
// by its entry action, and held for UI_SPLASH_MS), then two content pages -
// Demo and Stats - that take turns on UI_PAGE_MS. The LED events are handled
// one level up, so the model stays live while any page is up - the next
// page's entry action then draws the state as it is, not as it was.
// More pages (menu, diagnostics) grow as siblings of these two.

#define UI_SPLASH_MS 1500U   // how long the boot image holds the glass
#define UI_PAGE_MS   5000U   // how long a page holds the glass

typedef struct {
    SM_Hsm sm_hsm_;
    bool ledOn;          // last LED state posted by the Blinky AO
    uint16_t ledEdges;   // LED transitions seen; the status page seed
    bool splashUp;       // the glass belongs to the boot image
    TickType_t splashUntil;  // when the hold ends, while splashUp
} Sm_Ui;

void Sm_Ui_init(Sm_Ui * const me);
void Sm_Ui_dispatch(Sm_Ui * const me, Ui_Evt const * const e);

// Milliseconds left of the splash page's hold; zero once it is over (or when
// there was nothing to hold). The thread uses it for two things: how long it
// may sleep, and when to raise UI_SPLASH_DONE_SIG.
uint32_t Sm_Ui_splashLeft(Sm_Ui const * const me);

#endif // SM_UI_H_
