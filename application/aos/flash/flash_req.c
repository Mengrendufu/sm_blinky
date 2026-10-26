//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
//============================================================================
//=== Synchronous RPC adapter onto the Flash AO's SST event queue
//
// Requests remain on the caller's stack until completion. Event envelopes
// come from the SST pool: GC runs after dispatch returns, which may
// be after the caller has already resumed and discarded its request.

#include "flash.h"
#include "flash_req_priv.h"
#include "sm_assert.h"

SM_DEFINE_MODULE("flash_req")

//============================================================================
static unsigned Flash_inFlight_;
static SST_Task *Flash_target_;
//============================================================================
void Flash_reqBind(SST_Task *ao) {
    SM_REQUIRE(Flash_target_ == (SST_Task *)0);
    SM_REQUIRE(ao != (SST_Task *)0);
    Flash_target_ = ao;
}
//............................................................................
static bool Flash_post_(SST_Signal sig, Flash_Req * const req) {
    bool accepted = false;

    SM_REQUIRE(Flash_target_ != (SST_Task *)0);
    configASSERT(xPortIsInsideInterrupt() == pdFALSE);
    configASSERT(xTaskGetCurrentTaskHandle() != Flash_target_->thread);
    taskENTER_CRITICAL();
    if (Flash_inFlight_ < FLASH_REQUEST_CAPACITY) {
        ++Flash_inFlight_;
        Flash_Evt *evt = SST_NEW(Flash_Evt);
        evt->super.sig = sig;
        evt->req = req;
        SST_Task_post(Flash_target_, &evt->super);
        accepted = true;
    }
    taskEXIT_CRITICAL();
    return accepted;
}
//............................................................................
void Flash_reqFinish(Flash_Req * const req) {
    taskENTER_CRITICAL();
    SM_REQUIRE(Flash_inFlight_ > 0U);
    --Flash_inFlight_;
    taskEXIT_CRITICAL();
    // No request access after notification; the caller may resume now.
    // The AO still owns the event envelope until post-dispatch SST GC.
    (void)xTaskNotifyGiveIndexed(req->waiter, req->waiterIndex);
}
//............................................................................
// Caller side: submit, then give the CPU away until the flash thread is done.
// False means the RPC admission limit was reached: req.ok says nothing;
// the buffer was never touched.
static bool Flash_submit_(SST_Signal sig, Flash_Req * const req) {
    if (!Flash_post_(sig, req)) {
        return false;
    }

    // WAIT: the caller gives the CPU away here until the flash thread has
    // run the request to the end. Slot 1 is the only thing that can wake it,
    // which is why the caller's own entry slot had to stay free.
    (void)ulTaskNotifyTakeIndexed(req->waiterIndex, pdTRUE, portMAX_DELAY);

    return true;
}
//............................................................................
// Caller side: the caller-owned part. The caller is this task, and the call
// runs to the completion, so the slot is consumed and left clean before it
// returns; there is no stale notification for the next call to swallow.
static void Flash_initReq_(Flash_Req * const req) {
    req->ok = false;
    req->waiter = xTaskGetCurrentTaskHandle();
    req->waiterIndex = FLASH_DONE_INDEX;
    configASSERT(req->waiter != (TaskHandle_t)0);
}
//............................................................................
static bool Flash_call_(SST_Signal sig, uint32_t address, void *buf,
                        uint32_t len) {
    Flash_Req req;

    req.address = address;
    req.dst = buf;
    req.len = len;
    Flash_initReq_(&req);

    return Flash_submit_(sig, &req) && req.ok;
}
//============================================================================
// Raw NOR requests -- address is the physical flash byte address.
// These operations bypass NVS and do not resolve keys or select a partition.
bool Flash_readBlock(uint32_t address, void *dst, uint32_t len) {
    return Flash_call_(FLASH_READ_SIG, address, dst, len);
}
//............................................................................
// Any length, from anywhere: the flash thread splits it at page boundaries
// and waits the device out between them. The caller's buffer stays borrowed
// for the whole call.
bool Flash_writeBlock(uint32_t address, void const *src, uint32_t len) {
    return Flash_call_(FLASH_WRITE_SIG, address, (void *)(uintptr_t)src, len);
}
//............................................................................
// One 4 KB sector: the erase the write side needs and reads never do. The
// 130~160 ms the device takes are slept through, not spun.
bool Flash_eraseSector(uint32_t address) {
    return Flash_call_(FLASH_ERASE_SIG, address, (void *)0, 0U);
}
//============================================================================
// Application supplies the operation signal and logical item ID.
bool Flash_postNvm(SST_Signal sig, Nvm_Id id) {
    SM_REQUIRE(sig == FLASH_NVM_WRITE_SIG);
    SM_REQUIRE(id > NVM_ID_INVALID && (unsigned)id < NVM_ID_LIMIT);
    configASSERT(xPortIsInsideInterrupt() == pdFALSE);
    bool accepted = false;

    taskENTER_CRITICAL();
    if (Flash_target_ != NULL) {
        Flash_NvmEvt *evt = SST_NEW(Flash_NvmEvt);
        evt->super.sig = sig;
        evt->id = id;
        SST_Task_post(Flash_target_, &evt->super);
        accepted = true;
    }
    taskEXIT_CRITICAL();
    return accepted;
}
