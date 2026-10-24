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
// live in static storage: SST performs GC after dispatch returns, which may
// be after the caller has already resumed and discarded its request.

#include "flash.h"
#include "flash_req_priv.h"
#include "sm_assert.h"

SM_DEFINE_MODULE("flash_req")

//============================================================================
typedef struct {
    Flash_Evt event;
    bool used;
} Flash_RequestSlot_;

static Flash_RequestSlot_ Flash_slots_[FLASH_REQUEST_CAPACITY];
static SST_Task *Flash_target_;

//============================================================================
void Flash_reqBind(SST_Task *ao) {
    SM_REQUIRE(Flash_target_ == (SST_Task *)0);
    SM_REQUIRE(ao != (SST_Task *)0);
    for (unsigned i = 0U; i < FLASH_REQUEST_CAPACITY; ++i) {
        // Static SST headers remain immutable, even when an envelope is
        // reused before the previous dispatch's GC has inspected it.
        Flash_slots_[i].event.super.sig = FLASH_REQ_SIG;
    }
    Flash_target_ = ao;
}
//............................................................................
static bool Flash_post_(Flash_Req * const req) {
    bool accepted = false;

    SM_REQUIRE(Flash_target_ != (SST_Task *)0);
    configASSERT(xPortIsInsideInterrupt() == pdFALSE);
    configASSERT(xTaskGetCurrentTaskHandle() != Flash_target_->thread);
    taskENTER_CRITICAL();
    for (unsigned i = 0U; i < FLASH_REQUEST_CAPACITY; ++i) {
        Flash_RequestSlot_ * const slot = &Flash_slots_[i];

        if (!slot->used) {
            slot->used = true;
            slot->event.req = req;
            // Queue reserves space for every envelope plus checkpoint.
            SST_Task_post(Flash_target_, &slot->event.super);
            accepted = true;
            break;
        }
    }
    taskEXIT_CRITICAL();
    return accepted;
}
//............................................................................
void Flash_reqFinish(Flash_Req * const req) {
    bool released = false;

    taskENTER_CRITICAL();
    for (unsigned i = 0U; i < FLASH_REQUEST_CAPACITY; ++i) {
        Flash_RequestSlot_ * const slot = &Flash_slots_[i];

        if (slot->used && (slot->event.req == req)) {
            slot->used = false;
            released = true;
            break;
        }
    }
    taskEXIT_CRITICAL();
    SM_ASSERT(released);
    // No request or payload access after notification: the caller can run
    // immediately. The static envelope's SST header remains valid for GC.
    (void)xTaskNotifyGiveIndexed(req->waiter, req->waiterIndex);
}
//............................................................................
// Caller side: submit, then give the CPU away until the flash thread is done.
// False means all envelopes were in use: req.ok says nothing and the
// buffer was never touched.
static bool Flash_submit_(Flash_Req * const req) {
    if (!Flash_post_(req)) {
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
static bool Flash_call_(Flash_Op op, uint32_t address, void *buf,
                        uint32_t len) {
    Flash_Req req;

    req.op = op;
    req.address = address;
    req.dst = buf;
    req.len = len;
    Flash_initReq_(&req);

    return Flash_submit_(&req) && req.ok;
}
//============================================================================
// Raw NOR requests -- address is the physical flash byte address.
// These operations bypass NVS and do not resolve keys or select a partition.
bool Flash_readBlock(uint32_t address, void *dst, uint32_t len) {
    return Flash_call_(FLASH_OP_READ, address, dst, len);
}
//............................................................................
// Any length, from anywhere: the flash thread splits it at page boundaries
// and waits the device out between them. The caller's buffer stays borrowed
// for the whole call.
bool Flash_writeBlock(uint32_t address, void const *src, uint32_t len) {
    return Flash_call_(FLASH_OP_WRITE, address, (void *)(uintptr_t)src, len);
}
//............................................................................
// One 4 KB sector: the erase the write side needs and reads never do. The
// 130~160 ms the device takes are slept through, not spun.
bool Flash_eraseSector(uint32_t address) {
    return Flash_call_(FLASH_OP_ERASE, address, (void *)0, 0U);
}
//============================================================================
// NVS requests -- id is a logical key; len is the value's length in bytes.
// The shared envelope stores id in address. Only NVS operations interpret
// that field as a key; physical address resolution belongs to NVS.
bool Flash_nvsRead(uint16_t id, void *dst, uint32_t len) {
    return Flash_call_(FLASH_OP_NVS_READ, (uint32_t)id, dst, len);
}
//............................................................................
bool Flash_nvsWrite(uint16_t id, void const *src, uint32_t len) {
    return Flash_call_(FLASH_OP_NVS_WRITE, (uint32_t)id,
                       (void *)(uintptr_t)src, len);
}
