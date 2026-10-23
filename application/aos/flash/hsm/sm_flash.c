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
#include "flash_priv.h"
#include "flash_req.h"

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
void Sm_Flash_init(SST_Task * const me, SST_Evt const * const e) {
    Sm_Flash * const self = (Sm_Flash *)me;

    (void)e;
    SM_Hsm_init_(&self->sm_hsm_, (SM_InitHandler)Sm_Flash_TOP_initial_);
}
//............................................................................
void Sm_Flash_dispatch(SST_Task * const me, SST_Evt const * const e) {
    Sm_Flash * const self = (Sm_Flash *)me;

    SM_Hsm_dispatch_(&self->sm_hsm_, (void const *)e);
}
//............................................................................
// UML-Top-Initial
static SM_StatePtr Sm_Flash_TOP_initial_(SM_Hsm * const me) SM_HSM_RETT {
    (void)me;
    Flash_initStorage();
    Flash_armCheckpoint();
    return _SM_INIT(&Sm_Flash_ready);
}
//............................................................................
// State-Flash_ready
static SM_RetState Sm_Flash_ready_(void * const me,
                                   void const * const e) SM_HSM_RETT {
    SST_Evt const * const evt = (SST_Evt const *)e;

    (void)me;

    switch (evt->sig) {
        case FLASH_REQ_SIG: {
            Flash_Req * const req = SST_EVT_DOWNCAST(Flash_Evt, evt)->req;

            req->ok = false;

            switch (req->op) {
                // Raw NOR: use the physical byte address directly.
                case FLASH_OP_READ: {
                    req->ok = BSP_flashRead(BSP_flashDevice(),
                        req->address, req->dst, req->len);
                    break;
                }
                case FLASH_OP_WRITE: {
                    req->ok = BSP_flashWrite(BSP_flashDevice(),
                        req->address, req->dst, req->len);
                    break;
                }
                case FLASH_OP_ERASE: {
                    req->ok = BSP_flashEraseSector(
                        BSP_flashDevice(), req->address);
                    break;
                }

                // NVS: address carries a key ID, resolved by the store.
                case FLASH_OP_NVS_READ: {
                    req->ok = Flash_kvRead((uint16_t)req->address, req->dst,
                                           req->len);
                    break;
                }
                case FLASH_OP_NVS_WRITE: {
                    req->ok = Flash_kvWrite((uint16_t)req->address, req->dst,
                                            req->len);
                    break;
                }
                default: {
                    break;
                }
            }
            Flash_completeRequest(req);
            return _SM_HANDLED();
        }
        case FLASH_CHECKPOINT_SIG: {
            Flash_checkpoint();
            Flash_armCheckpoint();
            return _SM_HANDLED();
        }
        default: {
            return _SM_SUPER();
        }
    }
}
