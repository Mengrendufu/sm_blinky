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

//============================================================================
static SM_StatePtr Sm_Ui_TOP_initial_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Sm_Ui_running_(void * const me,
                                  void const * const e) SM_HSM_RETT;

// State-Ui_running
SM_HsmState SM_HSM_ROM Sm_Ui_running = {
    (SM_StatePtr)0,                  // super
    (SM_InitHandler)0,               // init_
    (SM_ActionHandler)0,             // entry_
    (SM_ActionHandler)0,             // exit_
    (SM_StateHandler)&Sm_Ui_running_ // handler
};

//============================================================================
void Sm_Ui_init(Sm_Ui * const me) {
    me->ledOn = false;
    me->ledEdges = 0U;
    SM_Hsm_init_(&me->sm_hsm_, (SM_InitHandler)Sm_Ui_TOP_initial_);
}
//............................................................................
void Sm_Ui_dispatch(Sm_Ui * const me, Ui_Evt const * const e) {
    SM_Hsm_dispatch_(&me->sm_hsm_, (void const *)e);
}
//............................................................................
// UML-Top-Initial
static SM_StatePtr Sm_Ui_TOP_initial_(SM_Hsm * const me) SM_HSM_RETT {
    (void)me;
    return _SM_INIT(&Sm_Ui_running);
}
//............................................................................
// State-Ui_running
static SM_RetState Sm_Ui_running_(void * const me,
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
