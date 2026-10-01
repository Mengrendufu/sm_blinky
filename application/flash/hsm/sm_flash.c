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
#include "bsp_flash.h"
#include "sm_flash.h"

//============================================================================
static SM_StatePtr Sm_Flash_TOP_initial_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Sm_Flash_ready_(void * const me,
                                   void const * const e) SM_HSM_RETT;

// State-Flash_ready
SM_HsmState SM_HSM_ROM Sm_Flash_ready = {
    (SM_StatePtr)0,                    // super
    (SM_InitHandler)0,                 // init_
    (SM_ActionHandler)0,               // entry_
    (SM_ActionHandler)0,               // exit_
    (SM_StateHandler)&Sm_Flash_ready_  // handler
};

//============================================================================
void Sm_Flash_init(Sm_Flash * const me) {
    SM_Hsm_init_(&me->sm_hsm_, (SM_InitHandler)Sm_Flash_TOP_initial_);
}
//............................................................................
void Sm_Flash_dispatch(Sm_Flash * const me, Flash_Evt const * const e) {
    SM_Hsm_dispatch_(&me->sm_hsm_, (void const *)e);
}
//............................................................................
// UML-Top-Initial
static SM_StatePtr Sm_Flash_TOP_initial_(SM_Hsm * const me) SM_HSM_RETT {
    (void)me;
    return _SM_INIT(&Sm_Flash_ready);
}
//............................................................................
// State-Flash_ready
static SM_RetState Sm_Flash_ready_(void * const me,
                                   void const * const e) SM_HSM_RETT {
    Flash_Evt const * const evt = (Flash_Evt const *)e;

    (void)me;

    switch (evt->sig) {
        case FLASH_REQ_SIG: {
            Flash_Req * const req = evt->req;

            req->ok = false;

            switch (req->op) {
                case FLASH_OP_PROBE: {
                    BSP_flashProbe();
                    req->ok = true;
                    break;
                }
                case FLASH_OP_READ: {
                    req->ok = BSP_flashRead(req->address,
                                            (uint8_t *)req->dst, req->len);
                    break;
                }
                default: {
                    break;
                }
            }
            return _SM_HANDLED();
        }
        default: {
            return _SM_SUPER();
        }
    }
}
