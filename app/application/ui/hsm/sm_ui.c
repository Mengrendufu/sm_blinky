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
#include "instance/nvm_blinky.h"
#include "instance/nvm_boot.h"
#include "nvm_port.h"

SM_DEFINE_MODULE("sm_ui")

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
    SM_REQUIRE(operations->readFps != NULL);
    SM_REQUIRE(operations->readCpuSample != NULL);
    SM_REQUIRE(operations->readThread != NULL);
    SM_REQUIRE(operations->readResource != NULL);
    me->display = operations;
}
//............................................................................
static void Sm_Ui_deliverTimeout_(void *context, void const *evt) {
    (void)context;
    Ui_post((SST_Evt const *)evt);
}

void Sm_Ui_ctor(Sm_Ui * const me) {
    static SST_Evt const stateTimeoutEvt = {
        .sig = UI_STATE_TIMEOUT_SIG, .poolId = 0U, .refCtr = 0U
    };
    static SST_Evt const statsTimeoutEvt = {
        .sig = UI_STATS_TIMEOUT_SIG, .poolId = 0U, .refCtr = 0U
    };
    static SST_Evt const threadTimeoutEvt = {
        .sig = UI_THREAD_TIMEOUT_SIG, .poolId = 0U, .refCtr = 0U
    };
    static SST_Evt const resourceTimeoutEvt = {
        .sig = UI_RESOURCE_TIMEOUT_SIG, .poolId = 0U, .refCtr = 0U
    };
    static TimeEvt_PostOperations const delivery = {
        .context = NULL, .post = Sm_Ui_deliverTimeout_
    };
    SM_REQUIRE(me != NULL);
    me->topbar = (UiTopbar){0};
    me->pageTitle = (UiPageTitle){0};
    me->bootCount = (UiBootCount){0};
    me->resourceMonitor = (UiResourceMonitor){0};
    TimeEvt_ctor(&me->stateTimeout, &stateTimeoutEvt, &delivery);
    TimeEvt_ctor(&me->statsTimeout, &statsTimeoutEvt, &delivery);
    TimeEvt_ctor(&me->threadTimeout, &threadTimeoutEvt, &delivery);
    TimeEvt_ctor(&me->resourceTimeout, &resourceTimeoutEvt, &delivery);
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
    UiTopbar_create(&self->topbar, self->root.container, lv_pct(100), 17);
    NVM_PORT_ENTER();
    uint32_t const edges = NvmBlinky_get()->edges;
    NVM_PORT_EXIT();
    UiTopbar_setCount(&self->topbar, edges);
    self->display->readCpuSample(self->display->context,
                                 &self->cpuIdle, &self->cpuTotal);
    uint32_t const fps = self->display->readFps(self->display->context);
    UiTopbar_setFps(&self->topbar, (uint16_t)(fps > 999U ? 999U : fps));
    TimeEvt_arm(&self->statsTimeout, UI_STATS_TICKS, UI_STATS_TICKS);
}

static SM_RetState Sm_Ui_active_(SM_Hsm * const me, SST_Evt const * const e) SM_HSM_RETT {
    Sm_Ui * const self = container_of(me, Sm_Ui, sm_hsm_);
    switch (e->sig) {
        case UI_STATS_TIMEOUT_SIG: {
            uint32_t idle, total;
            self->display->readCpuSample(self->display->context,
                                         &idle, &total);
            uint32_t const elapsed = total - self->cpuTotal;
            uint32_t const idleDelta = idle - self->cpuIdle;
            self->cpuIdle = idle;
            self->cpuTotal = total;
            if (elapsed != 0U) {
                SM_ASSERT(idleDelta <= elapsed);
                uint8_t const cpu = (uint8_t)(
                    ((uint64_t)(elapsed - idleDelta) * 100U
                     + elapsed / 2U) / elapsed);
                UiTopbar_setCpu(&self->topbar, cpu);
            }
            uint32_t const fps =
                self->display->readFps(self->display->context);
            UiTopbar_setFps(&self->topbar,
                            (uint16_t)(fps > 999U ? 999U : fps));
            return _SM_HANDLED();
        }
        case UI_LED_ON_SIG:
        case UI_LED_OFF_SIG: {
            UiTopbar_setLed(&self->topbar, e->sig == UI_LED_ON_SIG);
            NVM_PORT_ENTER();
            uint32_t const edges = NvmBlinky_get()->edges;
            NVM_PORT_EXIT();
            UiTopbar_setCount(&self->topbar, edges);
            return _SM_HANDLED();
        }
        default:
            return _SM_SUPER();
    }
}

static SM_StatePtr Sm_Ui_active_initial_(SM_Hsm * const me) SM_HSM_RETT {
    (void)me;
    return _SM_INIT(&Sm_Ui_sysInfo);
}

static void Sm_Ui_bindThread_(Sm_Ui *self, unsigned slot, unsigned index) {
    Sm_Ui_ThreadSample sample;
    self->display->readThread(self->display->context, index, &sample);
    SM_ASSERT(sample.name != NULL);
    SM_ASSERT(sample.stackBytes > 0U);
    SM_ASSERT(sample.stackMinBytes <= sample.stackBytes);
    UiThreadCard * const card = &self->threadMonitor.cards[slot];
    lv_snprintf(self->threadPriority[slot],
                 sizeof(self->threadPriority[slot]),
                 "P%lu", (unsigned long)sample.priority);
    lv_snprintf(self->threadStack[slot], sizeof(self->threadStack[slot]),
                 "min %lu/%lu", (unsigned long)sample.stackMinBytes,
                 (unsigned long)sample.stackBytes);
    UiThreadCard_setHeader(card, sample.name, self->threadPriority[slot]);
    UiThreadCard_setResource(card, UI_THREAD_CARD_STACK,
        self->threadStack[slot], "B",
        (int32_t)((uint64_t)sample.stackMinBytes * 100U / sample.stackBytes));
    int32_t queuePercent = 0;
    if (sample.queueKnown) {
        SM_ASSERT(sample.queueTotal > 0U);
        SM_ASSERT(sample.queueFree <= sample.queueTotal);
        lv_snprintf(self->threadQueue[slot],
            sizeof(self->threadQueue[slot]), "min %lu/%lu",
            (unsigned long)sample.queueFree,
            (unsigned long)sample.queueTotal);
        queuePercent = (int32_t)(
            (uint64_t)sample.queueFree * 100U / sample.queueTotal);
    }
    else if (sample.queueTotal != 0U) {
        lv_snprintf(self->threadQueue[slot],
            sizeof(self->threadQueue[slot]), "--/%lu",
            (unsigned long)sample.queueTotal);
    }
    else {
        lv_snprintf(self->threadQueue[slot],
                     sizeof(self->threadQueue[slot]), "N/A");
    }
    UiThreadCard_setResource(card, UI_THREAD_CARD_INBOX,
                             self->threadQueue[slot], "slots", queuePercent);
    lv_obj_set_style_opa(card->resources[UI_THREAD_CARD_INBOX].bar,
        sample.queueKnown ? LV_OPA_COVER : LV_OPA_30, LV_PART_MAIN);
}

static void Sm_Ui_bindResource_(Sm_Ui *self, unsigned slot, unsigned index) {
    Sm_Ui_ResourceSample sample;
    self->display->readResource(self->display->context, index, &sample);
    SM_ASSERT(sample.name != NULL);
    SM_ASSERT(sample.value <= sample.total);
    UiResourceCard * const card = &self->resourceMonitor.cards[slot];
    lv_obj_set_style_opa(card->arc,
        sample.total == 0U ? LV_OPA_30 : LV_OPA_COVER, LV_PART_MAIN);
    if (sample.total == 0U) {
        UiResourceCard_setHeader(card, sample.name, "--", "B");
        UiResourceCard_setGauge(card, "--%", "--", "--", 0);
        return;
    }
    uint32_t const percent = (uint32_t)(
        (uint64_t)sample.value * 100U / sample.total);
    lv_snprintf(self->resourceSize[slot], sizeof(self->resourceSize[slot]),
                 "%lu", (unsigned long)sample.blockBytes);
    lv_snprintf(self->resourcePercent[slot],
                 sizeof(self->resourcePercent[slot]),
                 "%lu%%", (unsigned long)percent);
    lv_snprintf(self->resourceValue[slot], sizeof(self->resourceValue[slot]),
                 "%lu", (unsigned long)sample.value);
    lv_snprintf(self->resourceTotal[slot], sizeof(self->resourceTotal[slot]),
                 "%lu", (unsigned long)sample.total);
    UiResourceCard_setHeader(card, sample.name,
                              self->resourceSize[slot], "B");
    UiResourceCard_setGauge(card, self->resourcePercent[slot],
        self->resourceValue[slot], self->resourceTotal[slot],
        (int32_t)percent);
}

static void Sm_Ui_sysInfo_entry_(SM_Hsm * const me) SM_HSM_RETT {
    Sm_Ui * const self = container_of(me, Sm_Ui, sm_hsm_);
    // Page-owned background; a missing slot leaves the root black.
    UiSysInfo_build(&self->root, &self->pageTitle,
                    &self->bootCount, &self->threadMonitor,
                    &self->resourceMonitor,
                    Asset_slot(Asset_get(), 0U));
    NVM_PORT_ENTER();
    uint32_t const boots = NvmBoot_get()->count;
    NVM_PORT_EXIT();
    UiBootCount_setCount(&self->bootCount, boots);
    self->threadIndex = 0U;
    self->threadSlot = 0U;
    Sm_Ui_bindThread_(self, 0U, 0U);
    Sm_Ui_bindThread_(self, 1U, 1U);
    self->resourceIndex = 0U;
    self->resourceSlot = 0U;
    Sm_Ui_bindResource_(self, 0U, 0U);
    Sm_Ui_bindResource_(self, 1U, 1U);
    TimeEvt_arm(&self->threadTimeout,
                 UI_THREAD_DWELL_TICKS, UI_THREAD_DWELL_TICKS);
    TimeEvt_arm(&self->resourceTimeout,
                 UI_THREAD_DWELL_TICKS, UI_THREAD_DWELL_TICKS);
    // TimeEvt_arm(&self->stateTimeout, UI_SYS_INFO_TICKS, 0U);
}

static void Sm_Ui_sysInfo_exit_(SM_Hsm * const me) SM_HSM_RETT {
    Sm_Ui * const self = container_of(me, Sm_Ui, sm_hsm_);
    (void)TimeEvt_disarm(&self->stateTimeout);
    (void)TimeEvt_disarm(&self->threadTimeout);
    (void)TimeEvt_disarm(&self->resourceTimeout);
    UiSysInfo_hide(&self->root, &self->pageTitle,
                   &self->bootCount, &self->threadMonitor,
                   &self->resourceMonitor);
}

static SM_RetState Sm_Ui_sysInfo_(SM_Hsm * const me, SST_Evt const * const e) SM_HSM_RETT {
    Sm_Ui * const self = container_of(me, Sm_Ui, sm_hsm_);
    switch (e->sig) {
        case UI_STATS_TIMEOUT_SIG:
            Sm_Ui_bindThread_(self, self->threadSlot, self->threadIndex);
            Sm_Ui_bindResource_(self, self->resourceSlot,
                                self->resourceIndex);
            return _SM_SUPER();
        case UI_RESOURCE_TIMEOUT_SIG: {
            if (self->resourceMonitor.sliding) {
                return _SM_HANDLED();
            }
            unsigned const next = self->resourceSlot ^ 1U;
            self->resourceIndex =
                (self->resourceIndex + 1U) % UI_RESOURCE_COUNT;
            Sm_Ui_bindResource_(self, next, self->resourceIndex);
            UiResourceMonitor_slide(&self->resourceMonitor,
                (uint8_t)self->resourceSlot, (uint8_t)next);
            self->resourceSlot = next;
            UiDots_setActive(&self->resourceMonitor.dots,
                              (uint8_t)self->resourceIndex);
            return _SM_HANDLED();
        }
        case UI_THREAD_TIMEOUT_SIG: {
            if (self->threadMonitor.sliding) {
                return _SM_HANDLED();
            }
            unsigned const next = self->threadSlot ^ 1U;
            self->threadIndex = (self->threadIndex + 1U) % UI_THREAD_COUNT;
            Sm_Ui_bindThread_(self, next, self->threadIndex);
            UiThreadMonitor_slide(&self->threadMonitor,
                (uint8_t)self->threadSlot, (uint8_t)next);
            self->threadSlot = next;
            UiDots_setActive(&self->threadMonitor.dots,
                              (uint8_t)self->threadIndex);
            return _SM_HANDLED();
        }
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
