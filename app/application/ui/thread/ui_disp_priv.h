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

// Initialize LVGL, its tick source, display/strip and LCD wait binding.
// Called once on the UI thread; all lv_* calls stay on that same thread.
void Ui_dispInit(void);

// Request a whole-screen redraw and track its final flush. The HSM owns
// page objects/backgrounds and decides when backlight becomes visible.
void Ui_dispRequestFrame(void);

// UI-thread operation; visibility timing belongs to HSM state actions.
void Ui_dispBacklight(bool on);

// Runs lv_timer_handler() (which draws whatever is dirty) and returns how
// long
// the thread may sleep before the next timer is due. Clamped both ways:
// LV_NO_TIMER_READY or anything above the poll fallback becomes the fallback,
// and zero becomes one tick, so the loop never spins.
TickType_t Ui_dispHeartbeat(void);

// What the last heartbeat cost, and the extremes it has shown. The stats page
// carries them; the measurement is the adapter's.
// Completed refreshes per second over the last one-second window.
uint32_t Ui_dispFps(void);
uint32_t Ui_dispFrameUs(void);
uint32_t Ui_dispFrameMin(void);
uint32_t Ui_dispFrameMax(void);

#endif // UI_DISP_PRIV_H_
