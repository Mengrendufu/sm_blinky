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
#include "bsp.h"
#include "bsp_lcd.h"
#include "bsp_sram.h"
#include "bsp_flash.h"
#include "application.h"
#include "ui_evt.h"
#include "flash.h"

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
// State-Blinky_on
static void Blinky_on_entry_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Blinky_on_(Blinky * const me,
                              SST_Evt const * const e) SM_HSM_RETT;
SM_HsmState SM_HSM_ROM Blinky_on = {
    (SM_StatePtr)0,              // super
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
    (SM_StatePtr)0,              // super
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
// this AO only blinked; the flash probe and its HAL SPI call depth run here
// now, so the stack follows that and is re-measured from [MEM].
#define BLINKY_STACK_WORDS 256U // 256 words = 1 KB
static StaticTask_t Blinky_tcb;
static StackType_t  Blinky_stack[BLINKY_STACK_WORDS];

// Periodic maintenance beats. One beat is 500 ms, so the numbers below are a
// stack report every 4 s and the bench probes 30 s in - late enough that a
// terminal is attached by then, which is the point of running them late.
#define BLINKY_MEM_BEATS  8U
#define BLINKY_PROBE_BEAT 60U

static uint16_t blinky_beats;

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
    // The panel and the SRAM come up in BSP_init(), before the scheduler, so
    // this AO owns nothing but its own heartbeat.
    SST_TimeEvt_arm(&container_of(me, Blinky, sm_hsm_)->timeEvt,
                    BSP_TICKS_PER_SEC / 2, BSP_TICKS_PER_SEC / 2);
    return _SM_INIT(&Blinky_on);
}
// State-Blinky_on
static void Blinky_on_entry_(SM_Hsm * const me) SM_HSM_RETT {
    (void)me;
    BSP_ledOn();
    (void)Ui_postLedOn();
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
    (void)Ui_postLedOff();
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
    uint16_t const beat = ++blinky_beats;

    if ((beat % BLINKY_MEM_BEATS) == 0U) {
        BSP_memReport(BSP_TRACE_STACK_BLINKY);
    }

    if (beat == BLINKY_PROBE_BEAT) {
        BSP_lcdCheck();
        BSP_sramBench();
        // The flash belongs to the flash thread at run time, so the probe
        // goes through its inbox like any other request.
        (void)Flash_probe();
        // The write self test still drives SPI2 from its own task. It is
        // scaffolding: once assets land it goes to FLASH_WT_OFF, and then the
        // flash thread is the only bus user.
        BSP_flashSelfTestStart();
    }

}
