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
#include "asset.h"
#include "application_evt.h"
#include "sst_pubsub.h"
#include "ui_evt.h"

//============================================================================
static SM_StatePtr Sm_Flash_TOP_initial_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Sm_Flash_bringUpStageOne_(void *me, void const *e);
static SM_RetState Sm_Flash_bringUpStageTwo_(void *me, void const *e);
static void Sm_Flash_bringUpStageTwo_entry_(SM_Hsm *me);
static void Sm_Flash_ready_entry_(SM_Hsm *me);
static SM_RetState Sm_Flash_ready_(void *me, void const *e);

SM_HsmState SM_HSM_ROM Sm_Flash_bringUpStageOne = {
    SM_HSM_TOP, 0, 0, 0, &Sm_Flash_bringUpStageOne_
};
SM_HsmState SM_HSM_ROM Sm_Flash_bringUpStageTwo = {
    SM_HSM_TOP, 0, (SM_ActionHandler)&Sm_Flash_bringUpStageTwo_entry_, 0,
    &Sm_Flash_bringUpStageTwo_
};
SM_HsmState SM_HSM_ROM Sm_Flash_ready = {
    SM_HSM_TOP, 0, (SM_ActionHandler)&Sm_Flash_ready_entry_, 0,
    &Sm_Flash_ready_
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
// TOP queues work only; its initial path runs before the scheduler.
static SM_StatePtr Sm_Flash_TOP_initial_(SM_Hsm * const me) SM_HSM_RETT {
    SST_Evt *start = SST_NEW(SST_Evt);
    start->sig = LOADING_SPLASH_ASSETS_SIG;
    Sm_Flash * const self = container_of(me, Sm_Flash, sm_hsm_);

    SST_Task_post(&self->super, start);
    return _SM_INIT(&Sm_Flash_bringUpStageOne);
}

static SM_RetState Sm_Flash_bringUpStageOne_(void *me, void const *e) {
    SST_Evt const * const evt = e;
    (void)me;

    switch (evt->sig) {
        case LOADING_SPLASH_ASSETS_SIG:
            Flash_bindRuntime();
            Asset_loadSplash();
            return _SM_TRAN(&Sm_Flash_bringUpStageTwo);
        default:
            return _SM_SUPER();
    }
}

// Each delivery owns its event; publish may recycle before it returns.
static void Sm_Flash_notify_(SST_Signal sig) {
    SST_Evt *evt = SST_NEW(SST_Evt);
    evt->sig = sig;
    SST_publish(evt);

    evt = SST_NEW(SST_Evt);
    evt->sig = sig;
    Ui_post(evt);
}

static void Sm_Flash_bringUpStageTwo_entry_(SM_Hsm *me) {
    SST_Evt *load = SST_NEW(SST_Evt);
    load->sig = LOADING_ASSETS_SIG;
    Sm_Flash * const self = container_of(me, Sm_Flash, sm_hsm_);

    SST_Task_post(&self->super, load);
    Sm_Flash_notify_(BRING_UP_READY_SIG);
}

static SM_RetState Sm_Flash_bringUpStageTwo_(void *me, void const *e) {
    SST_Evt const * const evt = e;
    (void)me;

    switch (evt->sig) {
        case LOADING_ASSETS_SIG:
            Asset_loadRemaining();
            return _SM_TRAN(&Sm_Flash_ready);
        default:
            return _SM_SUPER();
    }
}

static void Sm_Flash_ready_entry_(SM_Hsm *me) {
    (void)me;

    // Assets are complete; NVS restoration follows, as in the model.
    Sm_Flash_notify_(BRING_UP_ASSETS_LOADED_SIG);
    Flash_initStorage();
}

// Complete once; notifying the caller ends this handler's request borrow.
static void Sm_Flash_complete_(Flash_Req *req, bool ok) {
    req->ok = ok;
    Flash_completeRequest(req);
}

// State-Flash_ready
static SM_RetState Sm_Flash_ready_(void * const me,
                                   void const * const e) SM_HSM_RETT {
    SST_Evt const * const evt = (SST_Evt const *)e;

    (void)me;

    switch (evt->sig) {
        // Raw NOR: physical byte addresses.
        case FLASH_READ_SIG: {
            Flash_Req * const req = SST_EVT_DOWNCAST(Flash_Evt, evt)->req;
            Sm_Flash_complete_(req, BSP_flashRead(BSP_flashDevice(),
                req->address, req->dst, req->len));
            return _SM_HANDLED();
        }
        case FLASH_WRITE_SIG: {
            Flash_Req * const req = SST_EVT_DOWNCAST(Flash_Evt, evt)->req;
            Sm_Flash_complete_(req, BSP_flashWrite(BSP_flashDevice(),
                req->address, req->dst, req->len));
            return _SM_HANDLED();
        }
        case FLASH_ERASE_SIG: {
            Flash_Req * const req = SST_EVT_DOWNCAST(Flash_Evt, evt)->req;
            Sm_Flash_complete_(req, BSP_flashEraseSector(
                BSP_flashDevice(), req->address));
            return _SM_HANDLED();
        }
        case FLASH_NVM_WRITE_SIG: {
            Nvm_Id const id = SST_EVT_DOWNCAST(Flash_NvmEvt, evt)->id;
            Flash_writeNvm(id);
            return _SM_HANDLED();
        }
        default: {
            return _SM_SUPER();
        }
    }
}
