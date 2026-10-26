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
#include "time_evt.h"
#include "sm_ui_evt.h"
#include "views/ui_surface.h"
#include "views/ui_sys_page.h"
#include "views/ui_live_page.h"
#include "widgets/w_topbar.h"
#include "metrics/ui_metrics.h"

//============================================================================
//=== UI state machine
//
// One instance, owned by the UI thread and dispatched synchronously from its
// inbox drain -- never through an SST queue, so the UI thread is the only one
// that touches it.
//
// TOP enters standby. readySplash accepts GO after splash preparation;
// earlyReadyShow remembers content assets completing before GO. Splash and
// readyShow join the two one-shot events: hold elapsed and assets complete.
// Active starts at Sys; each page arms a one-shot page-switch event.
// Standby, Splash and Active all preserve the shared LED state/count.

// TimeEvt ticks follow the application's fixed 100 Hz timebase.
#define UI_SPLASH_TICKS 150U
#define UI_SYS_TICKS    500U
#define UI_LIVE_TICKS   1000U
#define UI_HEADER_TICKS 50U
#define UI_DIAL_TICKS   3U

// Required display capabilities, called synchronously on the UI thread.
// requestFrame submits a repaint request; it does not wait for rendering.
// readFps returns the adapter's cached completed-refresh rate.
// Bind before initialization; the immutable table/context outlive the HSM.
typedef struct {
    void *context;
    void (*requestFrame)(void *context);
    void (*setBacklight)(void *context, bool on);
    uint32_t (*readFps)(void *context);
} Sm_Ui_DisplayOperations;

typedef struct {
    SM_Hsm sm_hsm_;
    Sm_Ui_DisplayOperations const *display;
    // Registered before scheduling; states directly arm/disarm their nodes.
    TimeEvt stateTimeout;
    TimeEvt headerTimeout;
    TimeEvt dialTimeout;
    bool ledOn;
    uint32_t ledEdges;

    // UI subsystem storage: HSM coordinates these independent capabilities.
    // Physical embedding is not UML containment within a leaf Component.
    UiSurface surface;
    struct W_Topbar top;
    UiSysPage sys;
    UiLivePage live;
    UiMetrics metrics;
} Sm_Ui;

void Sm_Ui_bindDisplay(Sm_Ui * const me,
                       Sm_Ui_DisplayOperations const *operations);
// Construct once before the tick driver starts; timeouts queue to Ui_post.
void Sm_Ui_ctor(Sm_Ui * const me);
void Sm_Ui_init(Sm_Ui * const me);
void Sm_Ui_dispatch(Sm_Ui * const me, SST_Evt const * const e);

#endif // SM_UI_H_
