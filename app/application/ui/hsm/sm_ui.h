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
#include "widgets/ui_root/ui_root.h"
#include "widgets/topbar/topbar.h"
#include "widgets/page_title/page_title.h"
#include "widgets/boot_count/boot_count.h"
#include "widgets/thread_monitor/thread_monitor.h"
#include "widgets/resource_monitor/resource_monitor.h"

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
// Active starts at SysInfo; each page arms a one-shot page-switch event.
// Topbar plus the per-page content (SysInfo mounts a PageTitle
// with its BootCount group and ThreadMonitor) are the mounted content.

// TimeEvt ticks follow the application's fixed 100 Hz timebase.
#define UI_SPLASH_TICKS 150U
#define UI_SYS_INFO_TICKS    500U
#define UI_LIVE_SHOW_TICKS   1000U

// Required display capabilities, called synchronously on the UI thread.
// Bind before initialization; the immutable table/context outlive the HSM.
typedef struct {
    void *context;
    void (*setBacklight)(void *context, bool on);
} Sm_Ui_DisplayOperations;

typedef struct {
    SM_Hsm sm_hsm_;
    Sm_Ui_DisplayOperations const *display;
    // Registered before scheduling; states directly arm/disarm their nodes.
    TimeEvt stateTimeout;

    // The one persistent content container; Splash borrows its
    // background, no second object. Active puts it in COLUMN flex so
    // page content stacks under the shared Topbar.
    UiRoot root;
    // Passive Topbar, alive for the whole Active superstate; Sys and
    // Live share it, no per-page create/delete. No data sampling yet.
    UiTopbar topbar;
    // SysInfo-only page title; created on SysInfo entry, destroyed
    // on exit -- NOT shared across pages.
    UiPageTitle pageTitle;
    // SysInfo-only boot-count group, composed under pageTitle by
    // the page helper; count stays 0 (no NVM binding yet).
    UiBootCount bootCount;
    // SysInfo-owned two-card monitor; no resource sampling or animation.
    UiThreadMonitor threadMonitor;
    // SysInfo-owned lower skeleton monitor; no contents yet.
    UiResourceMonitor resourceMonitor;
} Sm_Ui;

void Sm_Ui_bindDisplay(Sm_Ui * const me,
                       Sm_Ui_DisplayOperations const *operations);
// Construct once before the tick driver starts; timeouts queue to Ui_post.
void Sm_Ui_ctor(Sm_Ui * const me);
void Sm_Ui_init(Sm_Ui * const me);
void Sm_Ui_dispatch(Sm_Ui * const me, SST_Evt const * const e);

#endif // SM_UI_H_
