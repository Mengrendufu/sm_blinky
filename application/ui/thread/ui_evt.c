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

//============================================================================
#define UI_INBOX_LEN 16U

static Ui_Evt Ui_inbox_[UI_INBOX_LEN];
static uint8_t Ui_head_;    // next slot to write
static uint8_t Ui_tail_;    // next slot to read
static uint8_t Ui_used_;
static TaskHandle_t Ui_thread_;

//============================================================================
void Ui_evtBind(TaskHandle_t thread) {
    Ui_thread_ = thread;
}
//............................................................................
static bool Ui_push_(Ui_Sig sig) {
    bool accepted = false;

    taskENTER_CRITICAL();
    if (Ui_used_ < UI_INBOX_LEN) {
        Ui_inbox_[Ui_head_].sig = sig;
        Ui_head_ = (uint8_t)((Ui_head_ + 1U) % UI_INBOX_LEN);
        ++Ui_used_;
        accepted = true;
    }
    taskEXIT_CRITICAL();

    return accepted;
}
//............................................................................
static void Ui_wake_(void) {
    TaskHandle_t const thread = Ui_thread_;

    if (thread != (TaskHandle_t)0) {
        (void)xTaskNotifyGive(thread);
    }
}
//............................................................................
bool Ui_evtPop(Ui_Evt * const out) {
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
bool Ui_postLedOn(void) {
    bool const accepted = Ui_push_(UI_LED_ON_SIG);

    if (accepted) {
        Ui_wake_();
    }

    return accepted;
}
//............................................................................
bool Ui_postLedOff(void) {
    bool const accepted = Ui_push_(UI_LED_OFF_SIG);

    if (accepted) {
        Ui_wake_();
    }

    return accepted;
}
