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

struct _lv_obj_t;

// Brings LVGL up on the calling thread: lv_init, the tick source, the 240x320
// display, the partial strip buffer and the page container. Called once, from
// the UI thread - every lv_* call in the firmware happens on that thread,
// because LV_OS_NONE has no locks at all.
void Ui_dispInit(void);

// Pushes the boot image and proves the push covered the frame: the page is
// cleared first, the coverage sentinel goes down, the image goes out, and the
// sentinel has to be gone afterwards. False means the glass is not showing
// the
// boot image, so there is nothing to hold.
bool Ui_dispSplash(void);

// The two parents the state machine builds into: the screen, where the chrome
// stays put for the whole run, and the page container, which a page change
// empties. Both are the display's, which is why they come from here.
struct _lv_obj_t *Ui_dispRoot(void);
struct _lv_obj_t *Ui_dispPageRoot(void);

// One page's life. Begin takes the last page's tree away, lays the background
// this page belongs to and asks for one whole-screen frame; end takes this
// page's tree away, so the two pages can never share the glass.
void Ui_dispPageBegin(uint32_t slot);
void Ui_dispPageEnd(void);

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
