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
//=== Inbox and the blocking call -- a plain static ring, no RTOS queue
//
// One consumer (the flash thread), any number of task producers. The ring
// holds pointers, not copies: the caller's request block is where the result
// has to land. A full ring refuses the request and says so, so a producer is
// never held up by the flash thread - and a caller that got false knows the
// work never ran and its buffer was not touched.
//
// Two task contexts share this file, which is what makes it read oddly at
// first: nobody notifies itself. Flash_wake_() runs in the caller and writes
// the flash thread's slot 0 ("there is work in the ring"), which the thread's
// loop takes. Flash_reqFinish() runs in the flash thread and writes the
// caller's slot 1 ("your request is done"), which the caller is parked on
// inside Flash_submit_().
//
//   caller (any task)          flash thread
//   ------------------         -------------
//   Flash_readBlock()
//     push, wake  ----------->  take(slot 0) returns
//     take(slot 1) [blocked]    pop, transfer
//     returns, reads req.ok  <--  finish -> give(slot 1)

#include "flash.h"
#include "flash_req_priv.h"

//============================================================================
#define FLASH_INBOX_LEN 8U

static Flash_Req *Flash_inbox_[FLASH_INBOX_LEN];
static uint8_t Flash_head_;   // next slot to write
static uint8_t Flash_tail_;   // next slot to read
static uint8_t Flash_used_;
static TaskHandle_t Flash_thread_;

//============================================================================
void Flash_reqBind(TaskHandle_t thread) {
    Flash_thread_ = thread;
}
//............................................................................
static bool Flash_push_(Flash_Req * const req) {
    bool accepted = false;

    taskENTER_CRITICAL();
    if (Flash_used_ < FLASH_INBOX_LEN) {
        Flash_inbox_[Flash_head_] = req;
        Flash_head_ = (uint8_t)((Flash_head_ + 1U) % FLASH_INBOX_LEN);
        ++Flash_used_;
        accepted = true;
    }
    taskEXIT_CRITICAL();

    return accepted;
}
//............................................................................
// Producer side: runs in the caller, and the slot it writes belongs to the
// flash thread. Nothing in this module notifies the current task here.
static void Flash_wake_(void) {
    TaskHandle_t const thread = Flash_thread_;

    if (thread != (TaskHandle_t)0) {
        (void)xTaskNotifyGiveIndexed(thread, FLASH_REQ_INDEX);
    }
}
//............................................................................
// Flash thread side: the single consumer, so the pop side needs no
// coordination beyond the critical section it shares with the producers.
bool Flash_reqPop(Flash_Req ** const out) {
    bool found = false;

    taskENTER_CRITICAL();
    if (Flash_used_ > 0U) {
        *out = Flash_inbox_[Flash_tail_];
        Flash_tail_ = (uint8_t)((Flash_tail_ + 1U) % FLASH_INBOX_LEN);
        --Flash_used_;
        found = true;
    }
    taskEXIT_CRITICAL();

    return found;
}
//............................................................................
// Flash thread side: the caller is parked on its own slot, so this give is
// what makes it runnable again. The buffer stays the caller's; this hands
// back a block its owner is still sitting on, which is exactly why it is
// safe.
void Flash_reqFinish(Flash_Req * const req) {
    (void)xTaskNotifyGiveIndexed(req->waiter, req->waiterIndex);
}
//............................................................................
// Caller side: submit, then give the CPU away until the flash thread is done.
// A return value of false means the request was refused (inbox full), so
// req.ok still says nothing and the buffer was never touched.
static bool Flash_submit_(Flash_Req * const req) {
    if (!Flash_push_(req)) {
        return false;
    }

    Flash_wake_();

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
bool Flash_probe(void) {
    Flash_Req req;

    req.op = FLASH_OP_PROBE;
    req.address = 0U;
    req.dst = (void *)0;
    req.len = 0U;
    Flash_initReq_(&req);

    return Flash_submit_(&req) && req.ok;
}
//............................................................................
bool Flash_readBlock(uint32_t address, void *dst, uint32_t len) {
    Flash_Req req;

    req.op = FLASH_OP_READ;
    req.address = address;
    req.dst = dst;
    req.len = len;
    Flash_initReq_(&req);

    return Flash_submit_(&req) && req.ok;
}
