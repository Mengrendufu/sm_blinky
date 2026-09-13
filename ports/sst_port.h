//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef SST_PORT_H_
#define SST_PORT_H_

//============================================================================
#include "stm32f1xx_hal.h"

//============================================================================
#include "FreeRTOS.h"
#include "task.h"

//============================================================================
#define SST_EVT_POOL_NUM 1U
#include "sst_evt_pool.h"

//============================================================================
// Mixed SST/FreeRTOS priority packing for this port:
//   low  byte  = SST logical priority (1..SST_MAX_TASK)
//   high byte  = FreeRTOS task priority
// Example: (24U << 8) | 1U  =>  SST=1, FreeRTOS=24
typedef uint16_t SST_TaskPrio;

//============================================================================
// additional SST-PORT task attributes for ARM Cortex-M
#define SST_PORT_TASK_ATTR \
    TaskHandle_t thread; \
    configSTACK_DEPTH_TYPE stackSizeWords;
//............................................................................
// additional SST-PORT task operations for ARM Cortex-M
#define SST_PORT_TASK_OPER                                                   \
    void SST_Task_activate(SST_Task * const me);                             \
    void SST_Task_setPrio(SST_Task * const me, SST_TaskPrio prio);           \
    bool SST_Task_postFromISR(SST_Task * const me,                           \
                              SST_Evt const * const e,                       \
                              BaseType_t * const pHigherPriorityTaskWoken);

//============================================================================
// SST-PORT critical section...
#define SST_PORT_CRIT_STAT
#define SST_PORT_CRIT_ENTRY() taskENTER_CRITICAL()
#define SST_PORT_CRIT_EXIT()  taskEXIT_CRITICAL()

// SST-PORT pend the Task after posting an eve
// @NOTE: executed inside SST critical section.
#define SST_PORT_TASK_PEND() ((void)xTaskNotifyGive(me->thread))

//============================================================================
// the idle SST callback for this SST port
void SST_onIdle(void);

//============================================================================
// the SST scheduler lock key type
typedef uint32_t SST_LockKey;

#endif // SST_PORT_H_
