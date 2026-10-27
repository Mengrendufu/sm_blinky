//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "sst.h"
#include "asset.h"
#include "bsp.h"
#include "bsp_lcd.h"
#include "bsp_flash.h"
#include "application.h"
#include "application_evt.h"
#include "sst_pubsub.h"
#include "ui_evt.h"
#include "sm_ui_evt.h"
#include "flash.h"
#include "nvm.h"
#include "sm_flash_evt.h"

// Class =====================================================================
typedef struct Blinky {
    SST_Task super;
    SM_Hsm sm_hsm_;
    SST_TimeEvt timeEvt;
} Blinky;

// Virtual ===================================================================
static void Blinky_init(Blinky * const me, SST_Evt const * const ie);
static void Blinky_dispatch(Blinky * const me, SST_Evt const * const e);
static void Blinky_maintenance_(void);

// SM_Hsm ====================================================================
// UML-Top-Initial
static SM_StatePtr Blinky_TOP_initial_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Blinky_inactive_(Blinky *me, SST_Evt const *e);
static SM_StatePtr Blinky_active_initial_(SM_Hsm *me);
static SM_RetState Blinky_active_(Blinky *me, SST_Evt const *e);
static SM_HsmState SM_HSM_ROM Blinky_inactive = {
    SM_HSM_TOP, 0, 0, 0, (SM_StateHandler)&Blinky_inactive_
};
static SM_HsmState SM_HSM_ROM Blinky_active = {
    SM_HSM_TOP, (SM_InitHandler)&Blinky_active_initial_, 0, 0,
    (SM_StateHandler)&Blinky_active_
};
// State-Blinky_on
static void Blinky_on_entry_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Blinky_on_(Blinky * const me,
                              SST_Evt const * const e) SM_HSM_RETT;
SM_HsmState SM_HSM_ROM Blinky_on = {
    &Blinky_active,              // super
    (SM_InitHandler)0,    // init_
    (SM_ActionHandler)&Blinky_on_entry_, // entry_
    (SM_ActionHandler)0,  // exit_
    (SM_StateHandler)&Blinky_on_         // handler
};
// State-Blinky-off
static void Blinky_off_entry_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Blinky_off_(Blinky * const me,
                               SST_Evt const * const e) SM_HSM_RETT;
SM_HsmState SM_HSM_ROM Blinky_off = {
    &Blinky_active,              // super
    (SM_InitHandler)0,    // init_
    (SM_ActionHandler)&Blinky_off_entry_, // entry_
    (SM_ActionHandler)0,  // exit_
    (SM_StateHandler)&Blinky_off_         // handler
};

// Instance ==================================================================
static Blinky Blinky_inst;
SST_Task * const AO_Blinky = &Blinky_inst.super;

// Task storage. The AO owns its stack, so its size is visible here and in the
// link map instead of hiding inside an RTOS heap. 128 words was enough when
// this AO only blinked, and the probes that once ran here needed the rest.
// Check the Sys page stack watermark before trimming it.
#define BLINKY_STACK_WORDS 256U // 256 words = 1 KB
static StaticTask_t Blinky_tcb;
static StackType_t  Blinky_stack[BLINKY_STACK_WORDS];

#define BLINKY_SAVE_BEATS 20U // 20 half-second beats = 10 s

static uint8_t blinky_saveBeats_;


// Class implementation ======================================================
void Blinky_ctor(void) {
    SST_Task_ctor(AO_Blinky,
                  (SST_Handler)&Blinky_init,
                  (SST_Handler)&Blinky_dispatch);
    AO_Blinky->tcb = &Blinky_tcb;
    AO_Blinky->stack = Blinky_stack;
    AO_Blinky->stackSizeWords = BLINKY_STACK_WORDS;
    SST_TimeEvt_ctor(&Blinky_inst.timeEvt, BLINKY_TIMEOUT_SIG, AO_Blinky);
}
//............................................................................
static void Blinky_init(Blinky * const me, SST_Evt const * const ie) {
    (void)ie;
    SM_Hsm_init_(&me->sm_hsm_, (SM_InitHandler)Blinky_TOP_initial_);
}
//............................................................................
static void Blinky_dispatch(Blinky * const me, SST_Evt const * const e) {
    // Translate *me and *e.
    SM_Hsm_dispatch_(&me->sm_hsm_, e);
}

// SM_Hsm ====================================================================
// UML-Top-Initial
static SM_StatePtr Blinky_TOP_initial_(SM_Hsm * const me) SM_HSM_RETT {
    // Subscribe before scheduler startup can execute Flash bring-up.
    SST_Task_subscribe(AO_Blinky, BRING_UP_READY_SIG);
    SST_Task_subscribe(AO_Blinky, BRING_UP_ASSETS_LOADED_SIG);
    SST_Task_subscribe(AO_Blinky, BRING_UP_NVM_READY_SIG);
    SST_TimeEvt_arm(&container_of(me, Blinky, sm_hsm_)->timeEvt,
                    BSP_TICKS_PER_SEC / 2, BSP_TICKS_PER_SEC / 2);
    return _SM_INIT(&Blinky_inactive);
}

static SM_RetState Blinky_inactive_(Blinky *me, SST_Evt const *e) {
    (void)me;
    switch (e->sig) {
        case BRING_UP_NVM_READY_SIG:
            (void)Flash_postNvm(FLASH_NVM_WRITE_SIG, NVM_BOOT_COUNT);
            return _SM_TRAN(&Blinky_active);
        default:
            return _SM_SUPER();
    }
}

static SM_StatePtr Blinky_active_initial_(SM_Hsm *me) {
    (void)me;
    return _SM_INIT(&Blinky_on);
}

static SM_RetState Blinky_active_(Blinky *me, SST_Evt const *e) {
    (void)me;
    (void)e;
    return _SM_SUPER();
}
// State-Blinky_on
static void Blinky_on_entry_(SM_Hsm * const me) SM_HSM_RETT {
    (void)me;
    BSP_ledOn();
    Nvm_addBlinky(1U);
    SST_Evt *evt = SST_NEW(SST_Evt);
    evt->sig = UI_LED_ON_SIG;
    Ui_post(evt);
}
static SM_RetState Blinky_on_(Blinky * const me,
                              SST_Evt const * const e) SM_HSM_RETT {
    switch (e->sig) {
        case BLINKY_TIMEOUT_SIG: {
            Blinky_maintenance_();
            return _SM_TRAN(&Blinky_off);
        }
        default: {
            return _SM_SUPER();
        }
    }
}
// State-Blinky-off
static void Blinky_off_entry_(SM_Hsm * const me) SM_HSM_RETT {
    (void)me;
    BSP_ledOff();
    Nvm_addBlinky(1U);
    SST_Evt *evt = SST_NEW(SST_Evt);
    evt->sig = UI_LED_OFF_SIG;
    Ui_post(evt);
}
static SM_RetState Blinky_off_(Blinky * const me,
                               SST_Evt const * const e) SM_HSM_RETT {
    switch (e->sig) {
        case BLINKY_TIMEOUT_SIG: {
            Blinky_maintenance_();
            return _SM_TRAN(&Blinky_on);
        }
        default: {
            return _SM_SUPER();
        }
    }
}
//............................................................................
// The same work in both states, so the beat counter lives in one place.
static void Blinky_maintenance_(void) {
    if (++blinky_saveBeats_ == BLINKY_SAVE_BEATS) {
        blinky_saveBeats_ = 0U;
        // The AO snapshots the counter when this event is handled.
        (void)Flash_postNvm(FLASH_NVM_WRITE_SIG, NVM_BLINKY_EDGES);
    }
}
