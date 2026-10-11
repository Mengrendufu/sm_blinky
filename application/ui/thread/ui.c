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
//=== UI thread -- wait, drain, dispatch

#include "bsp.h"
#include "ui.h"
#include "sm_ui.h"
#include "ui_disp_priv.h"
#include "ui_evt_priv.h"
#include "FreeRTOS.h"
#include "task.h"

//============================================================================
// Task storage: the UI owns its stack, so the size shows up here and in the
// link map instead of hiding inside an RTOS heap.
// LVGL renders on this stack and it is the deepest path in the firmware: at
// 1024 words the high water mark left 110 words free, so the old 512 word
// stack was overrun by roughly 400 - that was the frozen board. LVGL's own
// requirements page asks for "> 2 kB (> 8 kB recommended)" for this thread,
// and the measured 914 words is inside the minimum but nowhere near the
// recommendation, so the stack matches the recommendation. [MEM][STACK][UI]
// is re-read whenever the UI changes.
#define UI_STACK_WORDS  2048U // 2048 words = 8 KB
#define UI_PRIORITY     23U  // under the Blinky AO (24): the heartbeat wins

// LED edges between [MEM] reports, matching the AO's cadence so the two lines
// can be read side by side while the stack sizes are being settled.
#define UI_REPORT_EDGES 8U

static StaticTask_t Ui_tcb_;
static StackType_t Ui_stack_[UI_STACK_WORDS];
static Sm_Ui Ui_sm_;
static TaskHandle_t Ui_handle_;
static uint16_t Ui_reported_; // edge batches already reported

//============================================================================
static void Ui_thread_(void *arg);
static void Ui_drain_(void);
static void Ui_report_(void);

//============================================================================
void Ui_start(void) {
    // The state machine is initialized on its own thread, not here: its
    // initial transition is the splash page, whose entry action pushes the
    // boot image - a panel transfer to wait out, so it belongs on the thread.
    Ui_handle_ = xTaskCreateStatic(Ui_thread_, "ui", UI_STACK_WORDS,
                                   (void *)0, UI_PRIORITY,
                                   Ui_stack_, &Ui_tcb_);
    configASSERT(Ui_handle_ != (TaskHandle_t)0);

    Ui_evtBind(Ui_handle_);
}
//............................................................................
// The loop shape is the UI reference one: one blocking wait on (inbox wake |
// render deadline), then consume, then the phase work. Draining after every
// wake, timeout included, is what makes a lost notification harmless, and the
// deadline is whatever LVGL asks for next (clamped by the display adapter).
static void Ui_thread_(void *arg) {
    TickType_t wait;

    (void)arg;

    // LVGL comes up on the thread that drives it, so no other context ever
    // touches lv_*.
    Ui_dispInit();

    // The initial transition is the splash page: its entry action pushes the
    // boot image and arms the hold, and until the hold is over this loop runs
    // no LVGL at all.
    Sm_Ui_init(&Ui_sm_);
    wait = Ui_dispHeartbeat();

    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, wait);

        Ui_drain_();
        Ui_report_();

        // The hold is the state machine's; the thread only delivers its
        // deadline as an event, and the transition that follows starts the
        // demo page (clear the frame, prove it, draw it whole).
        if (Ui_sm_.splashUp && (Sm_Ui_splashLeft(&Ui_sm_) == 0U)) {
            Ui_Evt evt;

            evt.sig = UI_SPLASH_DONE_SIG;
            Sm_Ui_dispatch(&Ui_sm_, &evt);
        }

        // Nothing here touches the widgets while the boot image owns the
        // glass: a widget drawn then would be a widget on top of the image.
        if (!Ui_sm_.splashUp) {
            Ui_dispRender(Ui_sm_.ledOn, Ui_sm_.ledEdges);
        }

        wait = Ui_dispHeartbeat();
    }
}
//............................................................................
static void Ui_drain_(void) {
    Ui_Evt evt;

    while (Ui_evtPop(&evt)) {
        Sm_Ui_dispatch(&Ui_sm_, &evt);
    }
}
//............................................................................
static void Ui_report_(void) {
    uint16_t const batch = (uint16_t)(Ui_sm_.ledEdges / UI_REPORT_EDGES);

    if (batch != Ui_reported_) {
        Ui_reported_ = batch;
        BSP_dbgMemReport(BSP_TRACE_STACK_UI);
    }
}
