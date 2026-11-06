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
#include "widgets/live_motion/live_motion.h"
#include "widgets/live_signal/live_signal.h"

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

#define UI_THREAD_COUNT        5U
#define UI_RESOURCE_COUNT      5U

typedef struct {
    char const *name;
    uint32_t priority;
    uint32_t stackMinBytes;
    uint32_t stackBytes;
    uint32_t queueFree;
    uint32_t queueTotal;
    bool queueKnown;
} Sm_Ui_ThreadSample;

typedef struct {
    char const *name;
    uint32_t blockBytes;
    uint32_t value;
    // Zero means an unbound reserved resource; display unknown, not 0%.
    uint32_t total;
} Sm_Ui_ResourceSample;

// Required display capabilities, called synchronously on the UI thread.
// Bind before initialization; the immutable table/context outlive the HSM.
typedef struct {
    void *context;
    void (*setBacklight)(void *context, bool on);
    uint32_t (*readFps)(void *context);
    void (*readCpuSample)(void *context, uint32_t *idle, uint32_t *total);
    // Fixed roster: UI, Blinky, Flash, Idle, Timer daemon.
    void (*readThread)(void *context, unsigned index,
                        Sm_Ui_ThreadSample *sample);
    void (*readResource)(void *context, unsigned index,
                          Sm_Ui_ResourceSample *sample);
} Sm_Ui_DisplayOperations;

typedef struct {
    SM_Hsm sm_hsm_;
    Sm_Ui_DisplayOperations const *display;
    // Registered before scheduling; states directly arm/disarm their nodes.
    TimeEvt splashTimeout;
    TimeEvt pageSwitchTimeout;
    TimeEvt statsTimeout;
    TimeEvt cardSlideTimeout;
    TimeEvt animationTrigger;
    TimeEvt animationSwitchTimeout;
    unsigned resourceIndex;
    unsigned resourceSlot;
    char resourceSize[2][12];
    char resourcePercent[2][8];
    char resourceValue[2][12];
    char resourceTotal[2][12];
    unsigned threadIndex;
    unsigned threadSlot;
    // Card setters borrow these strings until the slot is rebound.
    char threadPriority[2][12];
    char threadStack[2][32];
    char threadQueue[2][32];
    uint32_t cpuIdle;
    uint32_t cpuTotal;

    // The one persistent content container; Splash borrows its
    // background, no second object. Its COLUMN flex stacks page
    // content under the shared Topbar.
    UiRoot root;
    // Passive Topbar, alive for the whole Active superstate; Sys and
    // Live share it; LED events refresh the state and NVM edge count.
    UiTopbar topbar;
    // Page-owned title; each page creates on entry and destroys on exit.
    UiPageTitle pageTitle;
    // SysInfo-only boot-count group, composed under pageTitle by
    // the page helper; reads the restored count on page entry.
    UiBootCount bootCount;
    // SysInfo-owned two-card monitor; sampled data and sliding pagination.
    UiThreadMonitor threadMonitor;
    // SysInfo-owned resource watermarks and sliding pagination.
    UiResourceMonitor resourceMonitor;
    // LiveShow-owned passive arc/bar composition.
    UiLiveMotion liveMotion;
    UiLiveSignal liveSignal;
} Sm_Ui;

void Sm_Ui_bindDisplay(Sm_Ui * const me,
                       Sm_Ui_DisplayOperations const *operations);
// Construct once before the tick driver starts; timeouts queue to Ui_post.
void Sm_Ui_ctor(Sm_Ui * const me);
void Sm_Ui_init(Sm_Ui * const me);
void Sm_Ui_dispatch(Sm_Ui * const me, SST_Evt const * const e);

#endif // SM_UI_H_
