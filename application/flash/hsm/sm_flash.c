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
#include "flash_req.h"

//============================================================================
//=== The read that lets the CPU go elsewhere
//
// One chunk is one transaction: chip select, command, address, then the DMA
// moves the bytes and the interrupt that ends it gives this thread's slot 1
// back. The thread parks on that slot between chunks, and that is the whole
// point: for the 15 ms a 32 KB chunk takes, the idle task runs and keeps
// draining the tracer. The blocking BSP_flashRead spins through the same
// bytes instead, which is the right thing before the scheduler exists - there
// is no idle task up there to starve.
#define FLASH_DMA_WAIT_MS 200U // one chunk measures ~15 ms

static TaskHandle_t Sm_Flash_dmaWaiter_;

// Interrupt context, from the receive channel's vector.
static void Sm_Flash_dmaDone_(bool ok) {
    BaseType_t woken = pdFALSE;

    (void)ok; // the caller reads BSP_flashDmaFailed() instead

    if (Sm_Flash_dmaWaiter_ != (TaskHandle_t)0) {
        (void)vTaskNotifyGiveIndexedFromISR(Sm_Flash_dmaWaiter_,
                                            FLASH_DONE_INDEX, &woken);
    }
    portYIELD_FROM_ISR(woken);
}
//............................................................................
static bool Sm_Flash_readDma_(uint32_t address, uint8_t *dst, uint32_t len) {
    bool ok = true;

    Sm_Flash_dmaWaiter_ = xTaskGetCurrentTaskHandle();

    while (ok && (len > 0U)) {
        uint32_t const n = (len > FLASH_DMA_CHUNK) ? FLASH_DMA_CHUNK : len;

        BSP_flashReadDma(address, dst, n, &Sm_Flash_dmaDone_);

        // WAIT: the transfer's own interrupt is the only thing that can wake
        // this thread here, and slot 0 is the inbox wake, so it has to be
        // slot 1. Never getting there means the interrupt never came.
        if (ulTaskNotifyTakeIndexed(FLASH_DONE_INDEX, pdTRUE,
                                    pdMS_TO_TICKS(FLASH_DMA_WAIT_MS)) == 0U) {
            SM_onAssert("fdma", __LINE__);
            ok = false;
            break;
        }

        ok = !BSP_flashDmaFailed();
        address += n;
        dst += n;
        len -= n;
    }

    Sm_Flash_dmaWaiter_ = (TaskHandle_t)0;

    return ok;
}
//............................................................................
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
                    req->ok = Sm_Flash_readDma_(req->address,
                                                (uint8_t *)req->dst,
                                                req->len);
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
