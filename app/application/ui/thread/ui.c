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
//=== UI thread -- wait, drain, dispatch

#include "bsp.h"
#include "ui.h"
#include "sm_ui.h"
#include "ui_disp_priv.h"
#include "ui_evt_priv.h"
#include "ui_evt.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "flash.h"
#include "aos/blinky/blinky.h"
#include "sm_tracer.h"

//============================================================================
// Task storage: the UI owns its stack, so the size shows up here and in the
// link map instead of hiding inside an RTOS heap.
// LVGL renders on this stack and it is the deepest path in the firmware: at
// 1024 words the high water mark left 110 words free, so the old 512 word
// stack was overrun by roughly 400 - that was the frozen board. LVGL's own
// requirements page asks for "> 2 kB (> 8 kB recommended)" for this thread,
// and the measured 914 words is inside the minimum but nowhere near the
// recommendation. Recheck stack watermarks when rebuilding UI widgets.
#define UI_PRIORITY     23U  // under the Blinky AO (24): the heartbeat wins

static StaticTask_t Ui_tcb_;
static StackType_t Ui_stack_[UI_STACK_WORDS];
static Sm_Ui Ui_sm_;
static TaskHandle_t Ui_handle_;

//============================================================================
static void Ui_thread_(void *arg);
static void Ui_drain_(void);
static void Ui_bindDisplay_(void);

//============================================================================
void Ui_start(void) {
    // Register the inbox and timeouts here. LVGL and HSM initialization
    // run later on the UI thread; TOP enters standby awaiting Flash events.
    Ui_handle_ = xTaskCreateStatic(Ui_thread_, "ui", UI_STACK_WORDS,
                                   (void *)0, UI_PRIORITY,
                                   Ui_stack_, &Ui_tcb_);
    configASSERT(Ui_handle_ != (TaskHandle_t)0);

    Ui_evtBind(Ui_handle_);
    Sm_Ui_ctor(&Ui_sm_);
}
//............................................................................
// The loop shape is the UI reference one: one blocking wait on (inbox wake |
// render deadline), then consume, then the phase work. Draining after every
// wake, timeout included, is what makes a lost notification harmless, and the
// deadline is whatever LVGL asks for next (clamped by the display adapter).
static void Ui_thread_(void *arg) {
    TickType_t wait;

    (void)arg;

    // LVGL comes up on the thread that drives it, so no other context ever
    // touches lv_*.
    Ui_dispInit();
    Ui_bindDisplay_();

    // Pending events wait until initialization completes before draining.
    Sm_Ui_init(&Ui_sm_);
    wait = Ui_dispHeartbeat();

    for (;;) {
        (void)ulTaskNotifyTakeIndexed(UI_INBOX_NOTIFY_INDEX, pdTRUE, wait);

        Ui_drain_();

        wait = Ui_dispHeartbeat();
    }
}
//............................................................................
static void Ui_drain_(void) {
    SST_Evt const *evt;

    while (Ui_evtPop(&evt)) {
        Sm_Ui_dispatch(&Ui_sm_, evt);
        SST_GC(evt);
    }
}
//............................................................................
//............................................................................
// The thread supplies the HSM's required display capabilities. These adapters
// keep the concrete display API out of the state machine.
static void Ui_setBacklight_(void *context, bool on) {
    (void)context;
    Ui_dispBacklight(on);
}

static uint32_t Ui_readFps_(void *context) {
    (void)context;
    return Ui_dispFps();
}

static void Ui_readCpuSample_(void *context,
                              uint32_t *idle, uint32_t *total) {
    (void)context;
    taskENTER_CRITICAL();
    BSP_cpuSample(idle, total);
    taskEXIT_CRITICAL();
}

static void Ui_readThread_(void *context, unsigned index,
                           Sm_Ui_ThreadSample *sample) {
    (void)context;
    configASSERT(index < UI_THREAD_COUNT && sample != NULL);
    TaskHandle_t const threads[UI_THREAD_COUNT] = {
        Ui_handle_, AO_Blinky->thread, AO_Flash->thread,
        xTaskGetIdleTaskHandle(), xTimerGetTimerDaemonTaskHandle()
    };
    configASSERT(threads[index] != NULL);
    TaskStatus_t status;
    vTaskGetInfo(threads[index], &status, pdTRUE, eInvalid);
    *sample = (Sm_Ui_ThreadSample){
        .name = status.pcTaskName,
        .priority = status.uxCurrentPriority,
        .stackMinBytes = status.usStackHighWaterMark * sizeof(StackType_t),
        .stackBytes = (uint32_t)(status.pxEndOfStack - status.pxStackBase
                                + 1U) * sizeof(StackType_t)
    };
    if (index == 0U) {
        sample->queueFree = Ui_evtInboxMinFree();
        sample->queueTotal = Ui_evtInboxLen();
        sample->queueKnown = true;
    }
    else if (index == 1U || index == 2U) {
        SST_Task const * const ao = index == 1U ? AO_Blinky : AO_Flash;
        taskENTER_CRITICAL();
        sample->queueTotal = (unsigned)ao->end + 1U;
        sample->queueFree = ao->queueMinFree;
        taskEXIT_CRITICAL();
        sample->queueKnown = true;
    }
    else if (index == 4U) {
        sample->queueKnown = BSP_timerQueueSample(
            &sample->queueFree, &sample->queueTotal);
    }
}

static void Ui_readResource_(void *context, unsigned index,
                             Sm_Ui_ResourceSample *sample) {
    (void)context;
    configASSERT(index < UI_RESOURCE_COUNT && sample != NULL);
    if (index < 2U) {
        SST_EvtPoolLevel const level = SST_EvtPool_level(index + 1U);
        *sample = (Sm_Ui_ResourceSample){
            .name = index == 0U ? "Small min free" : "Medium min free",
            .blockBytes = level.blockSize,
            .value = level.min, .total = level.total
        };
    }
    else if (index == 2U) {
        // Display slot only: no third SST pool is registered yet.
        *sample = (Sm_Ui_ResourceSample){.name = "Big reserved"};
    }
    else if (index == 3U) {
        taskENTER_CRITICAL();
        *sample = (Sm_Ui_ResourceSample){
            .name = "Tracer min free",
            .blockBytes = SM_Tracer_inst.blockSize,
            .value = SM_Tracer_inst.nMin, .total = SM_Tracer_inst.nTot
        };
        taskEXIT_CRITICAL();
    }
    else {
        lv_mem_monitor_t memory;
        lv_mem_monitor(&memory);
        *sample = (Sm_Ui_ResourceSample){
            .name = "LVGL peak used", .blockBytes = 1U,
            .value = (uint32_t)memory.max_used,
            .total = (uint32_t)memory.total_size
        };
    }
}

static void Ui_bindDisplay_(void) {
    static Sm_Ui_DisplayOperations const operations = {
        .context = NULL,
        .setBacklight = Ui_setBacklight_,
        .readFps = Ui_readFps_,
        .readCpuSample = Ui_readCpuSample_,
        .readThread = Ui_readThread_,
        .readResource = Ui_readResource_
    };

    Sm_Ui_bindDisplay(&Ui_sm_, &operations);
}
