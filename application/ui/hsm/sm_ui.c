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
#include "sm_ui.h"
#include "ui_disp_priv.h"
#include "FreeRTOS.h"
#include "task.h"

//============================================================================
static SM_StatePtr Sm_Ui_TOP_initial_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Sm_Ui_ui_(void * const me,
                             void const * const e) SM_HSM_RETT;
static void Sm_Ui_splash_entry_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Sm_Ui_splash_(void * const me,
                                 void const * const e) SM_HSM_RETT;
static void Sm_Ui_demo_entry_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Sm_Ui_demo_(void * const me,
                               void const * const e) SM_HSM_RETT;

// State-Ui: the level every page shares. The LED events land here, so the
// model keeps tracking the AO while the splash owns the glass - the demo
// page's first frame then draws the state as it is, not as it was when the
// widgets were built.
SM_HsmState SM_HSM_ROM Sm_Ui_ui = {
    SM_HSM_TOP,                       // super
    (SM_InitHandler)0,                // init_
    (SM_ActionHandler)0,              // entry_
    (SM_ActionHandler)0,              // exit_
    (SM_StateHandler)&Sm_Ui_ui_       // handler
};

// State-Splash: the boot image, pushed by the entry action and held for
// UI_SPLASH_MS. Nothing else may draw while this state is current - the
// display adapter is told the page, and the thread does not run LVGL at all.
SM_HsmState SM_HSM_ROM Sm_Ui_splash = {
    (SM_StatePtr)&Sm_Ui_ui,           // super
    (SM_InitHandler)0,                // init_
    (SM_ActionHandler)&Sm_Ui_splash_entry_,  // entry_
    (SM_ActionHandler)0,              // exit_
    (SM_StateHandler)&Sm_Ui_splash_   // handler
};

// State-Demo: the page the firmware lives in. Its entry asks the display
// adapter for a whole-screen repaint, which is where the first frame gets the
// splash's own treatment: cleared first, coverage proven.
SM_HsmState SM_HSM_ROM Sm_Ui_demo = {
    (SM_StatePtr)&Sm_Ui_ui,           // super
    (SM_InitHandler)0,                // init_
    (SM_ActionHandler)&Sm_Ui_demo_entry_,    // entry_
    (SM_ActionHandler)0,              // exit_
    (SM_StateHandler)&Sm_Ui_demo_     // handler
};

//============================================================================
void Sm_Ui_init(Sm_Ui * const me) {
    me->ledOn = false;
    me->ledEdges = 0U;
    me->splashUp = false;
    me->splashUntil = 0U;
    SM_Hsm_init_(&me->sm_hsm_, (SM_InitHandler)Sm_Ui_TOP_initial_);
}
//............................................................................
void Sm_Ui_dispatch(Sm_Ui * const me, Ui_Evt const * const e) {
    SM_Hsm_dispatch_(&me->sm_hsm_, (void const *)e);
}
//............................................................................
uint32_t Sm_Ui_splashLeft(Sm_Ui const * const me) {
    int32_t const left = (int32_t)(me->splashUntil - xTaskGetTickCount());

    return ((me->splashUp) && (left > 0)) ? (uint32_t)left : 0U;
}
//............................................................................
// UML-Top-Initial
static SM_StatePtr Sm_Ui_TOP_initial_(SM_Hsm * const me) SM_HSM_RETT {
    (void)me;
    return _SM_INIT(&Sm_Ui_splash);
}
//............................................................................
// State-Ui
static SM_RetState Sm_Ui_ui_(void * const me,
                             void const * const e) SM_HSM_RETT {
    Sm_Ui * const self = container_of((SM_Hsm *)me, Sm_Ui, sm_hsm_);
    Ui_Evt const * const evt = (Ui_Evt const *)e;

    switch (evt->sig) {
        case UI_LED_ON_SIG: {
            self->ledOn = true;
            ++self->ledEdges;
            return _SM_HANDLED();
        }
        case UI_LED_OFF_SIG: {
            self->ledOn = false;
            ++self->ledEdges;
            return _SM_HANDLED();
        }
        default: {
            return _SM_SUPER();
        }
    }
}
//............................................................................
// State-Splash
static void Sm_Ui_splash_entry_(SM_Hsm * const me) SM_HSM_RETT {
    Sm_Ui * const self = container_of((SM_Hsm *)me, Sm_Ui, sm_hsm_);

    // The page is the boot image: the display adapter clears the frame,
    // pushes it and proves the push covered it. A push that could not be
    // proven (no asset, or a frame the panel would not take whole) leaves no
    // hold: holding a glass that is showing something else is the ghost.
    self->splashUp = Ui_dispSplash();
    self->splashUntil = xTaskGetTickCount() + pdMS_TO_TICKS(UI_SPLASH_MS);
}
static SM_RetState Sm_Ui_splash_(void * const me,
                                 void const * const e) SM_HSM_RETT {
    Ui_Evt const * const evt = (Ui_Evt const *)e;

    switch (evt->sig) {
        case UI_SPLASH_DONE_SIG: {
            (void)me;
            return _SM_TRAN(&Sm_Ui_demo);
        }
        default: {
            return _SM_SUPER();   // the LED events are the shared level's
        }
    }
}
//............................................................................
// State-Demo
static void Sm_Ui_demo_entry_(SM_Hsm * const me) SM_HSM_RETT {
    Sm_Ui * const self = container_of((SM_Hsm *)me, Sm_Ui, sm_hsm_);

    self->splashUp = false;
    Ui_dispDemo();
}
static SM_RetState Sm_Ui_demo_(void * const me,
                               void const * const e) SM_HSM_RETT {
    (void)me;
    (void)e;
    return _SM_SUPER();
}
