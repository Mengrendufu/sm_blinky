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
//=== Flash thread -- wait, drain, dispatch

#include "bsp.h"
#include "flash.h"
#include "sm_flash.h"
#include "flash_req_priv.h"
#include "FreeRTOS.h"
#include "task.h"

//============================================================================
// Task storage: the thread owns its stack, so the size shows up here and in
// the link map instead of hiding inside an RTOS heap.
#define FLASH_STACK_WORDS 256U // 256 words = 1 KB; settle it from [MEM]

// Same level as the UI thread (23) and under the Blinky AO (24). It must not
// sit below its callers: a notification wait has no priority inheritance, so
// a middle-priority task in between would stretch every caller's block.
#define FLASH_PRIORITY    23U

// Completed requests between [MEM][STACK][FLASH] reports. The first request
// reports as well, so a boot that issues a single one still says how much
// stack the path took.
#define FLASH_REPORT_EVERY 8U

static StaticTask_t Flash_tcb_;
static StackType_t Flash_stack_[FLASH_STACK_WORDS];
static Sm_Flash Flash_sm_;
static TaskHandle_t Flash_handle_;
static uint32_t Flash_done_;

//============================================================================
static void Flash_thread_(void *arg);
static void Flash_drain_(void);
static void Flash_report_(void);

//============================================================================
void Flash_start(void) {
    Sm_Flash_init(&Flash_sm_);

    Flash_handle_ = xTaskCreateStatic(Flash_thread_, "flash",
                                      FLASH_STACK_WORDS, (void *)0,
                                      FLASH_PRIORITY, Flash_stack_,
                                      &Flash_tcb_);
    configASSERT(Flash_handle_ != (TaskHandle_t)0);

    Flash_reqBind(Flash_handle_);
}
//............................................................................
// No poll timeout: this thread has no periodic work at all, so it sleeps
// until a request arrives. That is the difference from an AO, which would
// have a beat to keep up with.
static void Flash_thread_(void *arg) {
    (void)arg;

    for (;;) {
        // WAIT: nothing pending, so the thread parks here and the CPU goes
        // elsewhere until a producer writes slot 0.
        (void)ulTaskNotifyTakeIndexed(FLASH_REQ_INDEX, pdTRUE, portMAX_DELAY);
        Flash_drain_();
    }
}
//............................................................................
static void Flash_drain_(void) {
    Flash_Req *req;

    while (Flash_reqPop(&req)) {
        Flash_Evt const evt = { FLASH_REQ_SIG, req };

        Sm_Flash_dispatch(&Flash_sm_, &evt);
        Flash_reqFinish(req);

        ++Flash_done_;
        Flash_report_();
    }
}
//............................................................................
static void Flash_report_(void) {
    if ((Flash_done_ == 1U) || ((Flash_done_ % FLASH_REPORT_EVERY) == 0U)) {
        BSP_dbgMemReport(BSP_TRACE_STACK_FLASH);
    }
}
