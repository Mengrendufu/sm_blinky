//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "dbc_assert.h"
#include "sst.h"
#include "sst_priv.h"

DBC_MODULE_NAME("sst_port")

//============================================================================
void SST_init(void) {
    /* 只做编译期 #error 断言，不调任何 FreeRTOS 运行时 API */
    /* 调度器未启动时不能调 xTask* 系列函数 */
    #if (configUSE_TASK_NOTIFICATIONS == 0)
        #error "SST FreeRTOS port requires configUSE_TASK_NOTIFICATIONS = 1"
    #endif // configUSE_TASK_NOTIFICATIONS == 0
    #if (INCLUDE_vTaskSuspend == 0)
        #error "SST FreeRTOS port requires INCLUDE_vTaskSuspend = 1"
    #endif // INCLUDE_vTaskSuspend == 0
}

//============================================================================
void SST_start(void) {
    //........................................................................
    // StaticEventPool...
    static STATIC_POOL_ELEM_TYPE(SST_Evt) smallPoolSto[16];
    SST_EvtPool_init(smallPoolSto, sizeof(smallPoolSto), sizeof(SST_Evt));
}
//............................................................................
static void SST_ao_thread(void *arg) {
    SST_Task *me = (SST_Task *)arg;   /* 每个任务通过 me 区分身份 */
    for (;;) {
        /* 等通知（计数信号量 -1），阻塞直到有事件 */
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
        /* 出队一个事件并 dispatch */
        SST_Task_activate(me);
    }
}
//............................................................................
void SST_Task_setPrio(SST_Task * const me, SST_TaskPrio prio) {
    /* 创建 FreeRTOS 任务（静态），me 作为 arg 传入，句柄存回 me->thread */
    /* prio: lo=SST, hi=FreeRTOS；SST 核心已用低字节做过范围检查 */
    /* 任务栈由 AO 自己声明并通过 tcb/stack/stackSizeWords 交进来 */
    uint8_t const sstPrio = (uint8_t)prio;
    uint8_t const freertosPrio = (uint8_t)(prio >> 8U);
    configSTACK_DEPTH_TYPE const stackWords = me->stackSizeWords;

    configASSERT(me->tcb != (StaticTask_t *)0);
    configASSERT(me->stack != (StackType_t *)0);
    configASSERT(stackWords > 0U);
    configASSERT(freertosPrio < (uint8_t)configMAX_PRIORITIES);
    configASSERT(SST_tasks_[sstPrio] == (SST_Task *)0);
    me->prio = sstPrio;
    SST_tasks_[sstPrio] = me;
    me->thread = xTaskCreateStatic(
        SST_ao_thread,      /* 所有 AO 共用同一入口 */
        "ao",
        stackWords,         /* 单位：word */
        me,                 /* ← me 作为 arg，入口函数里拿到身份 */
        (UBaseType_t)freertosPrio,
        me->stack,          /* ← AO 自己的栈 */
        me->tcb             /* ← AO 自己的 TCB */
    );
    configASSERT(me->thread != (TaskHandle_t)0);
}
//............................................................................
void SST_Task_activate(SST_Task * const me) {
    taskENTER_CRITICAL();
    if (me->nUsed == 0U) {           /* 队列为空（防御） */
        taskEXIT_CRITICAL();
        return;
    }
    SST_Evt const *e = me->qBuf[me->tail];
    if (me->tail == 0U) { me->tail = me->end; }
    else                { --me->tail; }
    --me->nUsed;
    taskEXIT_CRITICAL();
    (*me->dispatch)(me, e); /* dispatch 在临界区外 */
    // Event GC...
    SST_GC(e);
}
//............................................................................
bool SST_Task_postFromISR(SST_Task * const me,
                          SST_Evt const * const e,
                          BaseType_t * const pHigherPriorityTaskWoken)
{
    UBaseType_t isrStatus;

    DBC_REQUIRE(310, (me != (SST_Task *)0) &&
                     (me->nUsed <= me->end) &&
                     (e != (SST_Evt const *)0) &&
                     (pHigherPriorityTaskWoken != (BaseType_t *)0));

    isrStatus = taskENTER_CRITICAL_FROM_ISR();
    me->qBuf[me->head] = e;
    if (me->head == 0U) {
        me->head = me->end;
    } else {
        --me->head;
    }
    ++me->nUsed;
    taskEXIT_CRITICAL_FROM_ISR(isrStatus);

    // activate thread
    vTaskNotifyGiveFromISR(me->thread, pHigherPriorityTaskWoken);

    return true;
}

SST_LockKey SST_Task_lock(SST_TaskPrio ceiling)
{
    (void)ceiling;
    return 0;
}

void SST_Task_unlock(SST_LockKey lock_key)
{
    (void)lock_key;
}
