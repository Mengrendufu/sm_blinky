//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "ui_metrics.h"
#include "bsp.h"
#include "ui_evt_priv.h"
#include "flash.h"
#include "aos/blinky/blinky.h"
#include "sm_tracer.h"
#include "sst_port.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

extern uint8_t _ebss;
extern uint8_t _estack;
#define UI_RAM_BASE 0x20000000U
#define UI_METRIC_TASKS 8U

static char const * const UiMetrics_taskNames_[] = {
    "ui", NULL, NULL, "Tmr Svc", "IDLE"
};

void UiMetrics_init(UiMetrics *me) {
    *me = (UiMetrics){0};
    me->snapshot.ramUsed = (uint32_t)((uintptr_t)&_ebss - UI_RAM_BASE);
    me->snapshot.ramTotal = (uint32_t)((uintptr_t)&_estack - UI_RAM_BASE);
}

void UiMetrics_sample(UiMetrics *me) {
    static TaskStatus_t tasks[UI_METRIC_TASKS];
    configRUN_TIME_COUNTER_TYPE runTime;
    uint32_t idle;
    uint32_t total;

    BSP_cpuSample(&idle, &total);
    uint32_t const dTotal = total - me->total;
    uint32_t const dIdle = idle - me->idle;
    if (dTotal != 0U) {
        me->snapshot.cpuPct = 100U - ((dIdle * 100U) / dTotal);
    }
    me->idle = idle;
    me->total = total;

    UBaseType_t const n = uxTaskGetSystemState(tasks, UI_METRIC_TASKS,
                                               &runTime);
    for (unsigned row = 0U; row < UI_METRIC_TASK_ROWS; ++row) {
        UiResourceLevel * const level = &me->snapshot.rows[row];
        level->available = false;
        for (UBaseType_t i = 0U; i < n; ++i) {
            bool matches;
            if (row == 1U) {
                matches = tasks[i].xHandle == AO_Flash->thread;
            } else if (row == 2U) {
                matches = tasks[i].xHandle == AO_Blinky->thread;
            } else {
                matches = strcmp(tasks[i].pcTaskName,
                                  UiMetrics_taskNames_[row]) == 0;
            }
            if (matches) {
                level->total = (uint32_t)(tasks[i].pxEndOfStack
                                          - tasks[i].pxStackBase) + 1U;
                level->free = (uint32_t)tasks[i].usStackHighWaterMark;
                level->available = true;
                break;
            }
        }
    }
    me->snapshot.rows[5] = (UiResourceLevel){
        .free = (uint32_t)SM_Tracer_inst.nMin,
        .total = (uint32_t)SM_Tracer_inst.nTot, .available = true
    };
    me->snapshot.rows[6] = (UiResourceLevel){
        .free = Ui_evtInboxMinFree(), .total = Ui_evtInboxLen(),
        .available = true
    };
    me->snapshot.rows[7] = (UiResourceLevel){
        .free = SST_EvtPool_min(), .total = SST_EvtPool_total(),
        .available = true
    };
    ++me->snapshot.revision;
}
