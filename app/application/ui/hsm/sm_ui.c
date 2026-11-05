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
#include "helpers/ui_splash.h"
#include "helpers/ui_sys_info.h"
#include "ui_evt.h"
#include "asset.h"

SM_DEFINE_MODULE("sm_ui")

static SST_Evt const Sm_Ui_stateTimeoutEvt = {
    .sig = UI_STATE_TIMEOUT_SIG, .poolId = 0U, .refCtr = 0U
};

// == DEBUG: whether the HSM installed a verified Splash image. ==
volatile uint8_t Ui_dbgSplashOk_;

//============================================================================
// Forward declares and static state descriptors
// TOP-INIT
static SM_StatePtr Sm_Ui_TOP_initial_(SM_Hsm * const me) SM_HSM_RETT;

// standby
// Standby records the two readiness milestones before a GO is received.
static SM_RetState Sm_Ui_standby_(SM_Hsm * const me, SST_Evt const * const e) SM_HSM_RETT;
SM_HsmState SM_HSM_ROM Sm_Ui_standby = {
    (SM_StatePtr)SM_HSM_TOP,         // super
    (SM_InitHandler)NULL,            // init_
    (SM_ActionHandler)NULL,          // entry_
    (SM_ActionHandler)NULL,          // exit_
    (SM_StateHandler)&Sm_Ui_standby_ // handler
};

// readySplash
static void Sm_Ui_readySplash_entry_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Sm_Ui_readySplash_(SM_Hsm * const me, SST_Evt const * const e) SM_HSM_RETT;
SM_HsmState SM_HSM_ROM Sm_Ui_readySplash = {
    (SM_StatePtr)&Sm_Ui_standby,                 // super
    (SM_InitHandler)NULL,                        // init_
    (SM_ActionHandler)&Sm_Ui_readySplash_entry_, // entry_
    (SM_ActionHandler)NULL,                      // exit_
    (SM_StateHandler)&Sm_Ui_readySplash_         // handler
};

// earlyReadyShow
static SM_RetState Sm_Ui_earlyReadyShow_(SM_Hsm * const me, SST_Evt const * const e) SM_HSM_RETT;
SM_HsmState SM_HSM_ROM Sm_Ui_earlyReadyShow = {
    (SM_StatePtr)&Sm_Ui_readySplash,        // super
    (SM_InitHandler)NULL,                   // init_
    (SM_ActionHandler)NULL,                 // entry_
    (SM_ActionHandler)NULL,                 // exit_
    (SM_StateHandler)&Sm_Ui_earlyReadyShow_ // handler
};

// splashing
// State-Splashing: show the boot image and wait for its display period.
// Its one-shot timer stays armed across transitions into readyShow.
static void Sm_Ui_splashing_entry_(SM_Hsm * const me) SM_HSM_RETT;
static void Sm_Ui_splashing_exit_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Sm_Ui_splashing_(SM_Hsm * const me, SST_Evt const * const e) SM_HSM_RETT;
SM_HsmState SM_HSM_ROM Sm_Ui_splashing = {
    (SM_StatePtr)SM_HSM_TOP,                   // super
    (SM_InitHandler)NULL,                      // init_
    (SM_ActionHandler)&Sm_Ui_splashing_entry_, // entry_
    (SM_ActionHandler)&Sm_Ui_splashing_exit_,  // exit_
    (SM_StateHandler)&Sm_Ui_splashing_         // handler
};

// readyShow
// One of the two one-shot conditions has arrived; await the other.
static SM_RetState Sm_Ui_readyShow_(SM_Hsm * const me, SST_Evt const * const e) SM_HSM_RETT;
SM_HsmState SM_HSM_ROM Sm_Ui_readyShow = {
    (SM_StatePtr)&Sm_Ui_splashing,     // super
    (SM_InitHandler)NULL,              // init_
    (SM_ActionHandler)NULL,            // entry_
    (SM_ActionHandler)NULL,            // exit_
    (SM_StateHandler)&Sm_Ui_readyShow_ // handler
};

// active
// Active mounts the passive Topbar on the black root; child states
// own their page content and share that one bar.
static void Sm_Ui_active_entry_(SM_Hsm * const me) SM_HSM_RETT;
static SM_StatePtr Sm_Ui_active_initial_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Sm_Ui_active_(SM_Hsm * const me, SST_Evt const * const e) SM_HSM_RETT;
SM_HsmState SM_HSM_ROM Sm_Ui_active = {
    (SM_StatePtr)SM_HSM_TOP,                // super
    (SM_InitHandler)&Sm_Ui_active_initial_, // init_
    (SM_ActionHandler)&Sm_Ui_active_entry_, // entry_
    (SM_ActionHandler)NULL,                 // exit_
    (SM_StateHandler)&Sm_Ui_active_         // handler
};

// sysInfo
// SysInfo mounts the slot-0 page background; keep the page deadline.
static void Sm_Ui_sysInfo_entry_(SM_Hsm * const me) SM_HSM_RETT;
static void Sm_Ui_sysInfo_exit_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Sm_Ui_sysInfo_(SM_Hsm * const me, SST_Evt const * const e) SM_HSM_RETT;
SM_HsmState SM_HSM_ROM Sm_Ui_sysInfo = {
    (SM_StatePtr)&Sm_Ui_active,          // super
    (SM_InitHandler)NULL,                // init_
    (SM_ActionHandler)&Sm_Ui_sysInfo_entry_, // entry_
    (SM_ActionHandler)&Sm_Ui_sysInfo_exit_,  // exit_
    (SM_StateHandler)&Sm_Ui_sysInfo_         // handler
};

// liveShow
// LiveShow mounts the slot-1 page background; keep the page deadline.
static void Sm_Ui_liveShow_entry_(SM_Hsm * const me) SM_HSM_RETT;
static void Sm_Ui_liveShow_exit_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Sm_Ui_liveShow_(SM_Hsm * const me, SST_Evt const * const e) SM_HSM_RETT;
SM_HsmState SM_HSM_ROM Sm_Ui_liveShow = {
    (SM_StatePtr)&Sm_Ui_active,           // super
    (SM_InitHandler)NULL,                 // init_
    (SM_ActionHandler)&Sm_Ui_liveShow_entry_, // entry_
    (SM_ActionHandler)&Sm_Ui_liveShow_exit_,  // exit_
    (SM_StateHandler)&Sm_Ui_liveShow_         // handler
};

void Sm_Ui_bindDisplay(Sm_Ui * const me,
                       Sm_Ui_DisplayOperations const *operations) {
    SM_REQUIRE(me != NULL);
    SM_REQUIRE(operations != NULL);
    SM_REQUIRE(operations->setBacklight != NULL);
    me->display = operations;
}
//............................................................................
static void Sm_Ui_deliverTimeout_(void *context, void const *evt) {
    (void)context;
    Ui_post((SST_Evt const *)evt);
}

void Sm_Ui_ctor(Sm_Ui * const me) {
    static TimeEvt_PostOperations const delivery = {
        .context = NULL, .post = Sm_Ui_deliverTimeout_
    };
    SM_REQUIRE(me != NULL);
    me->topbar = (UiTopbar){0};
    me->pageTitle = (UiPageTitle){0};
    me->bootCount = (UiBootCount){0};
    me->resourceMonitor = (UiResourceMonitor){0};
    TimeEvt_ctor(&me->stateTimeout, &Sm_Ui_stateTimeoutEvt, &delivery);
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
    UiRoot_init(&self->root);
    return _SM_INIT(&Sm_Ui_standby);
}
//............................................................................
// State-Standby: UI remains responsive while Flash owns initialization.
static SM_RetState Sm_Ui_standby_(SM_Hsm * const me, SST_Evt const * const e) SM_HSM_RETT {
    (void)me;
    SST_Evt const * const evt = e;

    switch (evt->sig) {
        case BRING_UP_READY_SIG:
            return _SM_TRAN(&Sm_Ui_readySplash);
        default:
            return _SM_SUPER();
    }
}

static void Sm_Ui_post_(SST_Signal sig) {
    SST_Evt *evt = SST_NEW(SST_Evt);
    evt->sig = sig;
    Ui_post(evt);
}

static void Sm_Ui_readySplash_entry_(SM_Hsm * const me) SM_HSM_RETT {
    (void)me;

    // Automatic startup policy. A future key source can supply this same
    // event instead; entering earlyReadyShow does not re-enter this parent.
    Sm_Ui_post_(UI_GO_SPLASHING_SIG);
}

static SM_RetState Sm_Ui_readySplash_(SM_Hsm * const me, SST_Evt const * const e) SM_HSM_RETT {
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

static SM_RetState Sm_Ui_earlyReadyShow_(SM_Hsm * const me, SST_Evt const * const e) SM_HSM_RETT {
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
    bool const shown = UiSplash_show(&self->root, Asset_splash());
    Ui_dbgSplashOk_ = shown ? 1U : 0U;
    TimeEvt_arm(&self->stateTimeout, shown ? UI_SPLASH_TICKS : 1U, 0U);
}

static void Sm_Ui_splashing_exit_(SM_Hsm * const me) SM_HSM_RETT {
    Sm_Ui * const self = container_of(me, Sm_Ui, sm_hsm_);
    (void)TimeEvt_disarm(&self->stateTimeout);
    UiSplash_hide(&self->root);
}

static SM_RetState Sm_Ui_splashing_(SM_Hsm * const me, SST_Evt const * const e) SM_HSM_RETT {
    (void)me;
    SST_Evt const * const evt = e;

    switch (evt->sig) {
        case UI_STATE_TIMEOUT_SIG:
            return _SM_TRAN(&Sm_Ui_readyShow);
        case BRING_UP_ASSETS_LOADED_SIG:
            return _SM_TRAN(&Sm_Ui_readyShow);
        default:
            return _SM_SUPER();
    }
}

static SM_RetState Sm_Ui_readyShow_(SM_Hsm * const me, SST_Evt const * const e) SM_HSM_RETT {
    (void)me;
    SST_Evt const * const evt = e;

    switch (evt->sig) {
        case UI_STATE_TIMEOUT_SIG:
            return _SM_TRAN(&Sm_Ui_active);
        case BRING_UP_ASSETS_LOADED_SIG:
            return _SM_TRAN(&Sm_Ui_active);
        default:
            return _SM_SUPER();
    }
}
//............................................................................
// The shared Topbar is created once here and stays up for the whole
// demo; there is no production exit path out of Active, so no exit
// action destroys it. The component destroy API remains for tests.
static void Sm_Ui_active_entry_(SM_Hsm * const me) SM_HSM_RETT {
    Sm_Ui * const self = container_of(me, Sm_Ui, sm_hsm_);
    lv_obj_set_flex_flow(self->root.container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(self->root.container,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(self->root.container, 10,
                             LV_PART_MAIN);
    UiTopbar_create(&self->topbar, self->root.container);
}

static SM_RetState Sm_Ui_active_(SM_Hsm * const me, SST_Evt const * const e) SM_HSM_RETT {
    (void)me;
    (void)e;
    return _SM_SUPER();
}

static SM_StatePtr Sm_Ui_active_initial_(SM_Hsm * const me) SM_HSM_RETT {
    (void)me;
    return _SM_INIT(&Sm_Ui_sysInfo);
}

static void Sm_Ui_sysInfo_entry_(SM_Hsm * const me) SM_HSM_RETT {
    Sm_Ui * const self = container_of(me, Sm_Ui, sm_hsm_);
    // Page-owned background; a missing slot leaves the root black.
    UiSysInfo_build(&self->root, &self->pageTitle,
                    &self->bootCount, &self->threadMonitor,
                    &self->resourceMonitor,
                    Asset_slot(Asset_get(), 0U));
    TimeEvt_arm(&self->stateTimeout, UI_SYS_INFO_TICKS, 0U);
}

static void Sm_Ui_sysInfo_exit_(SM_Hsm * const me) SM_HSM_RETT {
    Sm_Ui * const self = container_of(me, Sm_Ui, sm_hsm_);
    (void)TimeEvt_disarm(&self->stateTimeout);
    UiSysInfo_hide(&self->root, &self->pageTitle,
                   &self->bootCount, &self->threadMonitor,
                   &self->resourceMonitor);
}

static SM_RetState Sm_Ui_sysInfo_(SM_Hsm * const me, SST_Evt const * const e) SM_HSM_RETT {
    (void)me;
    switch (e->sig) {
        case UI_STATE_TIMEOUT_SIG:
            return _SM_TRAN(&Sm_Ui_liveShow);
        default:
            return _SM_SUPER();
    }
}

static void Sm_Ui_liveShow_entry_(SM_Hsm * const me) SM_HSM_RETT {
    Sm_Ui * const self = container_of(me, Sm_Ui, sm_hsm_);
    // Page-owned background; a missing slot leaves the root black.
    UiRoot_setBackground(&self->root, Asset_slot(Asset_get(), 1U));
    TimeEvt_arm(&self->stateTimeout, UI_LIVE_SHOW_TICKS, 0U);
}

static void Sm_Ui_liveShow_exit_(SM_Hsm * const me) SM_HSM_RETT {
    Sm_Ui * const self = container_of(me, Sm_Ui, sm_hsm_);
    (void)TimeEvt_disarm(&self->stateTimeout);
    UiRoot_setBackground(&self->root, NULL);
}

static SM_RetState Sm_Ui_liveShow_(SM_Hsm * const me, SST_Evt const * const e) SM_HSM_RETT {
    (void)me;
    switch (e->sig) {
        case UI_STATE_TIMEOUT_SIG:
            return _SM_TRAN(&Sm_Ui_sysInfo);
        default:
            return _SM_SUPER();
    }
}
