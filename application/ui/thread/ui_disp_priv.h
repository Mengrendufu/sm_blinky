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
//=== Display adapter internals -- private to application/ui/thread

#ifndef UI_DISP_PRIV_H_
#define UI_DISP_PRIV_H_

#include <stdbool.h>
#include <stdint.h>
#include "FreeRTOS.h"

// Brings LVGL up on the calling thread: lv_init, the tick source, the 240x320
// display, the partial strip buffer and the demo widgets. Called once, from
// the UI thread - every lv_* call in the firmware happens on that thread,
// because LV_OS_NONE has no locks at all.
void Ui_dispInit(void);

// Runs lv_timer_handler() (which draws whatever is dirty) and returns how
// long the thread may sleep before the next timer is due. Clamped both ways:
// LV_NO_TIMER_READY or anything above the poll fallback becomes the fallback,
// and zero becomes one tick, so the loop never spins.
TickType_t Ui_dispHeartbeat(void);

// Pushes the HSM state into the widgets. Idempotent and cheap: an unchanged
// state returns without touching anything, so nothing is invalidated and the
// next lv_timer_handler() finds nothing to draw.
void Ui_dispRender(bool ledOn, uint16_t edges);

#endif // UI_DISP_PRIV_H_
