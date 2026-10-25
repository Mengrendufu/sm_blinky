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
//=== Inbox -- a plain static ring, no FreeRTOS queue and no allocation
//
// One consumer (the UI thread), any number of task producers. The critical
// section guards the indices; the wake is the thread's own notification slot,
// so a post costs nothing extra when several events are already pending.

#include "ui_evt.h"
#include "ui_evt_priv.h"
#include "sm_assert.h"

SM_DEFINE_MODULE("ui_evt")

//============================================================================
#define UI_INBOX_LEN 32U

static SST_Evt const *Ui_inbox_[UI_INBOX_LEN];
static uint8_t Ui_head_;    // next slot to write
static uint8_t Ui_tail_;    // next slot to read
static uint8_t Ui_used_;
static uint8_t Ui_minFree_ = UI_INBOX_LEN;
static TaskHandle_t Ui_thread_;

//============================================================================
//............................................................................
unsigned Ui_evtInboxMinFree(void) {
    unsigned free_;

    taskENTER_CRITICAL();
    free_ = (unsigned)Ui_minFree_;
    taskEXIT_CRITICAL();

    return free_;
}
//............................................................................
unsigned Ui_evtInboxLen(void) {
    return (unsigned)UI_INBOX_LEN;
}
//............................................................................
void Ui_evtBind(TaskHandle_t thread) {
    Ui_thread_ = thread;
}
//............................................................................
static void Ui_push_(SST_Evt const *evt) {
    taskENTER_CRITICAL();
    SM_REQUIRE(Ui_used_ < UI_INBOX_LEN);
    if (evt->poolId != 0U) {
        SM_REQUIRE(evt->poolId <= SST_EVT_POOL_NUM);
        SM_REQUIRE(evt->refCtr < UINT8_MAX);
        ++((SST_Evt *)evt)->refCtr;
    }
    Ui_inbox_[Ui_head_] = evt;
    Ui_head_ = (uint8_t)((Ui_head_ + 1U) % UI_INBOX_LEN);
    ++Ui_used_;
    uint8_t const freeSlots = (uint8_t)(UI_INBOX_LEN - Ui_used_);
    if (freeSlots < Ui_minFree_) {
        Ui_minFree_ = freeSlots;
    }
    taskEXIT_CRITICAL();
}
//............................................................................
static void Ui_wake_(void) {
    TaskHandle_t const thread = Ui_thread_;

    if (thread != (TaskHandle_t)0) {
        (void)xTaskNotifyGiveIndexed(thread, UI_INBOX_NOTIFY_INDEX);
    }
}
//............................................................................
bool Ui_evtPop(SST_Evt const **out) {
    bool found = false;

    taskENTER_CRITICAL();
    if (Ui_used_ > 0U) {
        *out = Ui_inbox_[Ui_tail_];
        Ui_tail_ = (uint8_t)((Ui_tail_ + 1U) % UI_INBOX_LEN);
        --Ui_used_;
        found = true;
    }
    taskEXIT_CRITICAL();

    return found;
}
//............................................................................
void Ui_post(SST_Evt const *evt) {
    SM_REQUIRE(Ui_thread_ != (TaskHandle_t)0);
    SM_REQUIRE(evt != NULL);
    Ui_push_(evt);
    Ui_wake_();
}
