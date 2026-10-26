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
#include "ui_evt.h"
#include "time_evt.h"
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
#define UI_PRIORITY     23U  // under the Blinky AO (24): the heartbeat wins

// LED edges between [MEM] reports, matching the AO's cadence so the two lines
// can be read side by side while the stack sizes are being settled.
#define UI_REPORT_EDGES 8U

static StaticTask_t Ui_tcb_;
static StackType_t Ui_stack_[UI_STACK_WORDS];
static Sm_Ui Ui_sm_;
static SST_Evt const * const Ui_timeoutEvents_[] = {
    &Sm_Ui_splashTimeoutEvt, &Sm_Ui_pageTimeoutEvt,
    &Sm_Ui_headerTimeoutEvt, &Sm_Ui_dialTimeoutEvt
};
static TimeEvt Ui_timeouts_[ARRAY_NELEM(Ui_timeoutEvents_)];
static TimeEvt Ui_timebase_;
static TaskHandle_t Ui_handle_;
static uint16_t Ui_reported_; // edge batches already reported

//============================================================================
static void Ui_thread_(void *arg);
static void Ui_drain_(void);
static void Ui_report_(void);
static void Ui_bindDisplay_(void);
static void Ui_bindTimeout_(void);
static void Ui_deliverTimeout_(void *context, void const *evt);

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
    static TimeEvt_PostOperations const delivery = {
        .context = NULL, .post = Ui_deliverTimeout_
    };
    static SST_Evt const timebase = {
        .sig = UI_TIMEOUT_SIG, .poolId = 0U, .refCtr = 0U
    };
    for (size_t i = 0U; i < ARRAY_NELEM(Ui_timeouts_); ++i) {
        TimeEvt_ctor(&Ui_timeouts_[i], Ui_timeoutEvents_[i], &delivery);
    }
    TimeEvt_ctor(&Ui_timebase_, &timebase, &delivery);
    TimeEvt_arm(&Ui_timebase_, 1U, 1U);
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
    Ui_bindDisplay_();
    Ui_bindTimeout_();

    // The fixed timebase is already running; pending pulses wait in the
    // inbox until initialization completes and this thread begins draining.
    Sm_Ui_init(&Ui_sm_);
    wait = Ui_dispHeartbeat();

    for (;;) {
        (void)ulTaskNotifyTakeIndexed(UI_INBOX_NOTIFY_INDEX, pdTRUE, wait);

        Ui_drain_();
        Ui_report_();

        wait = Ui_dispHeartbeat();
    }
}
//............................................................................
static void Ui_drain_(void) {
    SST_Evt const *evt;

    while (Ui_evtPop(&evt)) {
        Sm_Ui_dispatch(&Ui_sm_, evt);
        SST_GC(evt);
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
//............................................................................
// The thread supplies the HSM's required display capabilities. These adapters
// keep the concrete display API out of the state machine.
static void Ui_requestFrame_(void *context) {
    (void)context;
    Ui_dispRequestFrame();
}

static void Ui_setBacklight_(void *context, bool on) {
    (void)context;
    Ui_dispBacklight(on);
}

static uint32_t Ui_readFps_(void *context) {
    (void)context;
    return Ui_dispFps();
}

static void Ui_bindDisplay_(void) {
    static Sm_Ui_DisplayOperations const operations = {
        .context = NULL,
        .requestFrame = Ui_requestFrame_,
        .setBacklight = Ui_setBacklight_,
        .readFps = Ui_readFps_
    };

    Sm_Ui_bindDisplay(&Ui_sm_, &operations);
}

// The thread registers timer storage; the HSM chooses event and delay.
static void Ui_deliverTimeout_(void *context, void const *evt) {
    (void)context;
    Ui_post((SST_Evt const *)evt);
}

// Look up only pre-registered event identities; runtime arming never
// changes the registry or binds new delivery targets.
static TimeEvt *Ui_timeout_(SST_Evt const *event) {
    for (size_t i = 0U; i < ARRAY_NELEM(Ui_timeouts_); ++i) {
        if (Ui_timeoutEvents_[i] == event) {
            return &Ui_timeouts_[i];
        }
    }
    configASSERT(false);
    return NULL;
}

static void Ui_armTimeout_(void *context, SST_Evt const *event,
                            uint32_t ticks, uint32_t interval) {
    (void)context;
    TimeEvt_arm(Ui_timeout_(event), ticks, interval);
}

static void Ui_disarmTimeout_(void *context, SST_Evt const *event) {
    (void)context;
    (void)TimeEvt_disarm(Ui_timeout_(event));
}

static void Ui_bindTimeout_(void) {
    static Sm_Ui_TimeoutOperations const operations = {
        .context = NULL,
        .arm = Ui_armTimeout_,
        .disarm = Ui_disarmTimeout_
    };

    Sm_Ui_bindTimeout(&Ui_sm_, &operations);
}
