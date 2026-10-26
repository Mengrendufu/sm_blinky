//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "sm_port.h"
#include "sm_assert.h"
#include "sm_ui.h"
#include "ui_evt.h"
#include "asset.h"
#include "nvm.h"

SM_DEFINE_MODULE("sm_ui")

SST_Evt const Sm_Ui_splashTimeoutEvt = {
    .sig = UI_SPLASH_TIMEOUT_SIG, .poolId = 0U, .refCtr = 0U
};
SST_Evt const Sm_Ui_pageTimeoutEvt = {
    .sig = UI_PAGE_TIMEOUT_SIG, .poolId = 0U, .refCtr = 0U
};
SST_Evt const Sm_Ui_headerTimeoutEvt = {
    .sig = UI_HEADER_TIMEOUT_SIG, .poolId = 0U, .refCtr = 0U
};
SST_Evt const Sm_Ui_dialTimeoutEvt = {
    .sig = UI_DIAL_TIMEOUT_SIG, .poolId = 0U, .refCtr = 0U
};

// == DEBUG: whether the HSM installed a verified Splash image. ==
volatile uint8_t Ui_dbgSplashOk_;

static SM_RetState Sm_Ui_standby_(void *me, void const *e);
static void Sm_Ui_readySplash_entry_(SM_Hsm *me);
static SM_RetState Sm_Ui_readySplash_(void *me, void const *e);
static SM_RetState Sm_Ui_earlyReadyShow_(void *me, void const *e);
static SM_RetState Sm_Ui_readyShow_(void *me, void const *e);

static SM_StatePtr Sm_Ui_TOP_initial_(SM_Hsm * const me) SM_HSM_RETT;
static void Sm_Ui_splashing_entry_(SM_Hsm * const me) SM_HSM_RETT;
static void Sm_Ui_splashing_exit_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Sm_Ui_splashing_(void * const me,
                                 void const * const e) SM_HSM_RETT;
static void Sm_Ui_active_entry_(SM_Hsm *me);
static void Sm_Ui_active_exit_(SM_Hsm *me);
static SM_StatePtr Sm_Ui_active_initial_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Sm_Ui_active_(void * const me,
                                 void const * const e) SM_HSM_RETT;
static void Sm_Ui_sys_entry_(SM_Hsm * const me) SM_HSM_RETT;
static void Sm_Ui_sys_exit_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Sm_Ui_sys_(void * const me,
                              void const * const e) SM_HSM_RETT;
static void Sm_Ui_live_entry_(SM_Hsm * const me) SM_HSM_RETT;
static void Sm_Ui_live_exit_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Sm_Ui_live_(void * const me,
                               void const * const e) SM_HSM_RETT;


// Standby records the two readiness milestones before a GO is received.
SM_HsmState SM_HSM_ROM Sm_Ui_standby = {
    SM_HSM_TOP, 0, 0, 0, &Sm_Ui_standby_
};
SM_HsmState SM_HSM_ROM Sm_Ui_readySplash = {
    &Sm_Ui_standby, 0, (SM_ActionHandler)&Sm_Ui_readySplash_entry_, 0,
    &Sm_Ui_readySplash_
};
SM_HsmState SM_HSM_ROM Sm_Ui_earlyReadyShow = {
    &Sm_Ui_readySplash, 0, 0, 0, &Sm_Ui_earlyReadyShow_
};

// State-Splashing: show the boot image and wait for its display period.
// Its one-shot timer stays armed across transitions into readyShow.
SM_HsmState SM_HSM_ROM Sm_Ui_splashing = {
    SM_HSM_TOP,           // super
    (SM_InitHandler)0,                // init_
    (SM_ActionHandler)&Sm_Ui_splashing_entry_,  // entry_
    (SM_ActionHandler)&Sm_Ui_splashing_exit_,   // exit_
    (SM_StateHandler)&Sm_Ui_splashing_   // handler
};

// One of the two one-shot conditions has arrived; await the other.
SM_HsmState SM_HSM_ROM Sm_Ui_readyShow = {
    &Sm_Ui_splashing, 0, 0, 0, &Sm_Ui_readyShow_
};

// State-Active: shared header and LED model; pages own their transitions.
SM_HsmState SM_HSM_ROM Sm_Ui_active = {
    SM_HSM_TOP,                 // super
    (SM_InitHandler)&Sm_Ui_active_initial_, // init_
    (SM_ActionHandler)&Sm_Ui_active_entry_, // entry_
    (SM_ActionHandler)&Sm_Ui_active_exit_,  // exit_
    (SM_StateHandler)&Sm_Ui_active_         // handler
};

// State-Sys: page 1, the system's own numbers - the CPU as a half gauge and
// one bar per thread and per pool. Its entry owns every widget on it.
SM_HsmState SM_HSM_ROM Sm_Ui_sys = {
    (SM_StatePtr)&Sm_Ui_active,       // super
    (SM_InitHandler)0,                // init_
    (SM_ActionHandler)&Sm_Ui_sys_entry_,     // entry_
    (SM_ActionHandler)&Sm_Ui_sys_exit_,      // exit_
    (SM_StateHandler)&Sm_Ui_sys_      // handler
};

// State-Live: arc sweep, LED-driven switch, NVM count and static RAM size.
SM_HsmState SM_HSM_ROM Sm_Ui_live = {
    (SM_StatePtr)&Sm_Ui_active,       // super
    (SM_InitHandler)0,                // init_
    (SM_ActionHandler)&Sm_Ui_live_entry_,    // entry_
    (SM_ActionHandler)&Sm_Ui_live_exit_,     // exit_
    (SM_StateHandler)&Sm_Ui_live_     // handler
};

void Sm_Ui_bindDisplay(Sm_Ui * const me,
                       Sm_Ui_DisplayOperations const *operations) {
    SM_REQUIRE(me != NULL);
    SM_REQUIRE(operations != NULL);
    SM_REQUIRE(operations->requestFrame != NULL);
    SM_REQUIRE(operations->setBacklight != NULL);
    SM_REQUIRE(operations->readFps != NULL);
    me->display = operations;
}
//............................................................................
void Sm_Ui_bindTimeout(Sm_Ui * const me,
                       Sm_Ui_TimeoutOperations const *operations) {
    SM_REQUIRE(me != NULL);
    SM_REQUIRE(operations != NULL);
    SM_REQUIRE(operations->arm != NULL && operations->disarm != NULL);
    me->timeout = operations;
}
//............................................................................
void Sm_Ui_init(Sm_Ui * const me) {
    SM_Hsm_init_(&me->sm_hsm_, (SM_InitHandler)Sm_Ui_TOP_initial_);
}
//............................................................................
void Sm_Ui_dispatch(Sm_Ui * const me, SST_Evt const * const e) {
    SM_Hsm_dispatch_(&me->sm_hsm_, (void const *)e);
}
//............................................................................
static SM_StatePtr Sm_Ui_TOP_initial_(SM_Hsm * const me) SM_HSM_RETT {
    Sm_Ui * const self = container_of(me, Sm_Ui, sm_hsm_);

    SM_REQUIRE(self->display != NULL);
    SM_REQUIRE(self->timeout != NULL);
    self->ledOn = false;
    self->ledEdges = 0U;
    self->nvmPublished = 0U;
    UiMetrics_init(&self->metrics);
    UiSurface_init(&self->surface);
    W_Topbar_init(&self->top);
    UiSysPage_init(&self->sys);
    UiLivePage_init(&self->live);
    return _SM_INIT(&Sm_Ui_standby);
}
//............................................................................
// State-Standby: UI remains responsive while Flash owns initialization.
static SM_RetState Sm_Ui_standby_(void *me, void const *e) {
    Sm_Ui * const self = container_of((SM_Hsm *)me, Sm_Ui, sm_hsm_);
    SST_Evt const * const evt = e;

    switch (evt->sig) {
        case BRING_UP_READY_SIG:
            return _SM_TRAN(&Sm_Ui_readySplash);
        case UI_LED_ON_SIG:
            self->ledOn = true;
            ++self->ledEdges;
            return _SM_HANDLED();
        case UI_LED_OFF_SIG:
            self->ledOn = false;
            ++self->ledEdges;
            return _SM_HANDLED();
        default:
            return _SM_SUPER();
    }
}

static void Sm_Ui_readySplash_entry_(SM_Hsm *me) {
    static SST_Evt const go = {
        .sig = UI_GO_SPLASHING_SIG, .poolId = 0U, .refCtr = 0U
    };
    (void)me;

    // Automatic startup policy. A future key source can supply this same
    // event instead; entering earlyReadyShow does not re-enter this parent.
    Ui_post(&go);
}

static SM_RetState Sm_Ui_readySplash_(void *me, void const *e) {
    SST_Evt const * const evt = e;

    switch (evt->sig) {
        case UI_GO_SPLASHING_SIG:
            return _SM_TRAN(&Sm_Ui_splashing);
        case BRING_UP_ASSETS_LOADED_SIG:
            return _SM_TRAN(&Sm_Ui_earlyReadyShow);
        default:
            return _SM_SUPER();
    }
}

static SM_RetState Sm_Ui_earlyReadyShow_(void *me, void const *e) {
    SST_Evt const * const evt = e;

    switch (evt->sig) {
        case UI_GO_SPLASHING_SIG:
            return _SM_TRAN(&Sm_Ui_readyShow);
        default:
            return _SM_SUPER();
    }
}

// The hold belongs to this parent, including its readyShow substate.
static void Sm_Ui_splashing_entry_(SM_Hsm * const me) SM_HSM_RETT {
    Sm_Ui * const self = container_of(me, Sm_Ui, sm_hsm_);

    self->display->setBacklight(self->display->context, true);
    bool const shown = UiSurface_showSplash(&self->surface, Asset_splash());
    Ui_dbgSplashOk_ = shown ? 1U : 0U;
    self->display->requestFrame(self->display->context);
    self->timeout->arm(self->timeout->context, &Sm_Ui_splashTimeoutEvt,
                       shown ? UI_SPLASH_TICKS : 1U, 0U);
}

static void Sm_Ui_splashing_exit_(SM_Hsm * const me) SM_HSM_RETT {
    Sm_Ui * const self = container_of(me, Sm_Ui, sm_hsm_);
    self->timeout->disarm(self->timeout->context, &Sm_Ui_splashTimeoutEvt);
}

static SM_RetState Sm_Ui_splashing_(void * const me,
                                    void const * const e) SM_HSM_RETT {
    Sm_Ui * const self = container_of((SM_Hsm *)me, Sm_Ui, sm_hsm_);
    SST_Evt const * const evt = e;

    switch (evt->sig) {
        case UI_SPLASH_TIMEOUT_SIG:
            return _SM_TRAN(&Sm_Ui_readyShow);
        case BRING_UP_ASSETS_LOADED_SIG:
            return _SM_TRAN(&Sm_Ui_readyShow);
        case UI_LED_ON_SIG:
            self->ledOn = true;
            ++self->ledEdges;
            return _SM_HANDLED();
        case UI_LED_OFF_SIG:
            self->ledOn = false;
            ++self->ledEdges;
            return _SM_HANDLED();
        default:
            return _SM_SUPER();
    }
}

static SM_RetState Sm_Ui_readyShow_(void *me, void const *e) {
    (void)me;
    SST_Evt const * const evt = e;

    switch (evt->sig) {
        case UI_SPLASH_TIMEOUT_SIG:
            return _SM_TRAN(&Sm_Ui_active);
        case BRING_UP_ASSETS_LOADED_SIG:
            return _SM_TRAN(&Sm_Ui_active);
        default:
            return _SM_SUPER();
    }
}
//............................................................................
// Active owns the common header's lifetime and refresh event. Page states
// never invoke header-refresh helpers or inspect another page's state.
static void Sm_Ui_active_entry_(SM_Hsm *me) {
    Sm_Ui * const self = container_of(me, Sm_Ui, sm_hsm_);

    UiMetrics_sample(&self->metrics);
    W_Topbar_create(&self->top, UiSurface_overlay(&self->surface), "");
    W_Topbar_setLed(&self->top, self->ledOn);
    W_Topbar_setCpu(&self->top, self->metrics.snapshot.cpuPct);
    W_Topbar_setFps(&self->top,
        self->display->readFps(self->display->context));
    self->timeout->arm(self->timeout->context, &Sm_Ui_headerTimeoutEvt,
                       UI_HEADER_TICKS, UI_HEADER_TICKS);
}

static void Sm_Ui_active_exit_(SM_Hsm *me) {
    Sm_Ui * const self = container_of(me, Sm_Ui, sm_hsm_);
    self->timeout->disarm(self->timeout->context, &Sm_Ui_headerTimeoutEvt);
    W_Topbar_destroy(&self->top);
}

static SM_StatePtr Sm_Ui_active_initial_(SM_Hsm * const me) SM_HSM_RETT {
    (void)me;
    return _SM_INIT(&Sm_Ui_sys);
}

static SM_RetState Sm_Ui_active_(void * const me,
                                 void const * const e) SM_HSM_RETT {
    Sm_Ui * const self = container_of(me, Sm_Ui, sm_hsm_);
    SST_Evt const * const evt = e;

    switch (evt->sig) {
        case UI_HEADER_TIMEOUT_SIG:
            UiMetrics_sample(&self->metrics);
            W_Topbar_setCpu(&self->top, self->metrics.snapshot.cpuPct);
            W_Topbar_setFps(&self->top,
                self->display->readFps(self->display->context));
            return _SM_HANDLED();
        case UI_LED_ON_SIG:
            self->ledOn = true;
            ++self->ledEdges;
            W_Topbar_setLed(&self->top, true);
            return _SM_HANDLED();
        case UI_LED_OFF_SIG:
            self->ledOn = false;
            ++self->ledEdges;
            W_Topbar_setLed(&self->top, false);
            return _SM_HANDLED();
        default:
            return _SM_SUPER();
    }
}

static void Sm_Ui_sys_entry_(SM_Hsm * const me) SM_HSM_RETT {
    Sm_Ui * const self = container_of(me, Sm_Ui, sm_hsm_);

    UiSurface_showPage(&self->surface, Asset_slot(Asset_get(), 0U));
    W_Topbar_setTitle(&self->top, "sys");
    UiSysPage_create(&self->sys, UiSurface_content(&self->surface));
    UiSysPage_refresh(&self->sys, &self->metrics.snapshot);
    self->display->requestFrame(self->display->context);
    self->timeout->arm(self->timeout->context, &Sm_Ui_pageTimeoutEvt,
                       UI_SYS_TICKS, 0U);
}

static void Sm_Ui_sys_exit_(SM_Hsm * const me) SM_HSM_RETT {
    Sm_Ui * const self = container_of(me, Sm_Ui, sm_hsm_);
    self->timeout->disarm(self->timeout->context, &Sm_Ui_pageTimeoutEvt);
    UiSysPage_destroy(&self->sys);
}

static SM_RetState Sm_Ui_sys_(void * const me,
                              void const * const e) SM_HSM_RETT {
    Sm_Ui * const self = container_of(me, Sm_Ui, sm_hsm_);
    SST_Evt const * const evt = e;

    switch (evt->sig) {
        case UI_PAGE_TIMEOUT_SIG:
            return _SM_TRAN(&Sm_Ui_live);
        case UI_TIMEOUT_SIG:
            UiSysPage_refresh(&self->sys, &self->metrics.snapshot);
            return _SM_HANDLED();
        default:
            return _SM_SUPER();
    }
}

// Preserve the cumulative snapshot's restore/add semantics. Persistence is
// independent of any component's changed/unchanged rendering result.
static void Sm_Ui_publish_(Sm_Ui *self) {
    uint32_t const delta = self->ledEdges - self->nvmPublished;
    if (delta != 0U) {
        Nvm_addBlinky(delta);
        self->nvmPublished = self->ledEdges;
    }
}

static void Sm_Ui_live_entry_(SM_Hsm * const me) SM_HSM_RETT {
    Sm_Ui * const self = container_of(me, Sm_Ui, sm_hsm_);

    UiSurface_showPage(&self->surface, Asset_slot(Asset_get(), 1U));
    W_Topbar_setTitle(&self->top, "live");
    UiLivePage_create(&self->live, UiSurface_content(&self->surface),
        self->metrics.snapshot.ramUsed, self->metrics.snapshot.ramTotal);
    UiLivePage_setLed(&self->live, self->ledOn);
    Sm_Ui_publish_(self);
    UiLivePage_setCounter(&self->live, Nvm_blinky());
    self->display->requestFrame(self->display->context);
    self->timeout->arm(self->timeout->context, &Sm_Ui_pageTimeoutEvt,
                       UI_LIVE_TICKS, 0U);
    self->timeout->arm(self->timeout->context, &Sm_Ui_dialTimeoutEvt,
                       UI_DIAL_TICKS, UI_DIAL_TICKS);
}

static void Sm_Ui_live_exit_(SM_Hsm * const me) SM_HSM_RETT {
    Sm_Ui * const self = container_of(me, Sm_Ui, sm_hsm_);
    self->timeout->disarm(self->timeout->context, &Sm_Ui_dialTimeoutEvt);
    self->timeout->disarm(self->timeout->context, &Sm_Ui_pageTimeoutEvt);
    UiLivePage_destroy(&self->live);
}

static SM_RetState Sm_Ui_live_(void * const me,
                               void const * const e) SM_HSM_RETT {
    Sm_Ui * const self = container_of(me, Sm_Ui, sm_hsm_);
    SST_Evt const * const evt = e;

    switch (evt->sig) {
        case UI_PAGE_TIMEOUT_SIG:
            return _SM_TRAN(&Sm_Ui_sys);
        case UI_DIAL_TIMEOUT_SIG:
            UiLivePage_stepArc(&self->live);
            return _SM_HANDLED();
        case UI_TIMEOUT_SIG:
            Sm_Ui_publish_(self);
            UiLivePage_setCounter(&self->live, Nvm_blinky());
            return _SM_HANDLED();
        case UI_LED_ON_SIG:
            UiLivePage_setLed(&self->live, true);
            return _SM_SUPER(); // Active updates shared model/header once.
        case UI_LED_OFF_SIG:
            UiLivePage_setLed(&self->live, false);
            return _SM_SUPER();
        default:
            return _SM_SUPER();
    }
}
