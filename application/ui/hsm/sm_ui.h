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

#include "lvgl.h"
#include "sm_hsm.h"
#include "sm_ui_evt.h"
#include "widgets/w_barpanel.h"
#include "widgets/w_dots.h"
#include "widgets/w_gauge.h"
#include "widgets/w_nvm.h"
#include "widgets/w_ram.h"
#include "widgets/w_dial.h"
#include "widgets/w_switch.h"
#include "widgets/w_topbar.h"

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
// Active starts at Sys; its fixed-pulse guards continue alternating pages.
// Standby, Splash and Active all preserve the shared LED state/count.

#define UI_SPLASH_MS 1500U   // how long the boot image holds the glass
#define UI_PAGE_MS   8000U   // how long a page holds the glass (the live
                             // page's arc needs 7.2 s: up, then down to half)

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
    uint32_t timeoutCount; // pulses in the current splash/content page
    bool ledOn;          // last LED state posted by the Blinky AO
    uint32_t ledEdges;   // LED transitions seen, and the NVM_Blinky value:
                         // 32 bits on purpose - at two a second a 16 bit
                         // counter wraps in nine hours, which would hide the
                         // whole point of counting

    // The widgets this machine owns: the topbar once, for every page, and one
    // set per page that the page's own state builds and drops. The handles
    // live here because the states are the only thing that knows which page
    // is up; the wrappers in hsm/widgets do the LVGL work.
    // Page objects and image descriptors live as long as this HSM.
    lv_obj_t *pageRoot;
    lv_image_dsc_t bgDsc;
    lv_image_dsc_t splashDsc;
    struct W_Topbar top;      // title, cpu readout, heartbeat led
    struct W_Gauge gauge;     // page 1: the cpu half gauge
    struct W_BarPanel panel;  // page 1: one row per thread and per pool
    struct W_Dots dots;       // both pages: which page is up
    struct W_Dial dial;       // page 2: the arc that steps round
    struct W_Switch sw;       // page 2: flips with the AO's led
    struct W_Ram ram;         // page 2: the on-chip RAM, from the linker
    struct W_Nvm nvm;         // page 2: the blinky count, out of the NOR

    // What the widgets last showed, so an unchanged model costs nothing: no
    // property writes, no invalidation, no redraw.
    bool shownOn;
    uint32_t shownEdges;
    bool shownValid;

    // The slow window: when the readouts were last rewritten, and the CPU
    // sampler's two tick counters with the percentage they come to.
    uint32_t statAt;
    // The dial's own clock and where it stands: it steps five times a second,
    // which is the fastest an arc may redraw here (see w_dial.h).
    uint32_t dialAt;
    uint16_t dialPct;
    bool dialUp;         // the arc's direction: up to full, then to half
    // NVM_Blinky as the page last put it on the glass. The count itself
    // lives in the snapshot (application/nvm), where the flash task picks it
    // up every ten seconds.
    uint32_t nvmBlinky;
    uint32_t nvmPublished;    // ledEdges as last handed to the snapshot
    uint32_t cpuIdle;
    uint32_t cpuTotal;
    uint32_t cpuPct;
} Sm_Ui;

void Sm_Ui_bindDisplay(Sm_Ui * const me,
                       Sm_Ui_DisplayOperations const *operations);
void Sm_Ui_init(Sm_Ui * const me);
void Sm_Ui_dispatch(Sm_Ui * const me, SST_Evt const * const e);

#endif // SM_UI_H_
