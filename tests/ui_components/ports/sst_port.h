#pragma once
#include "task.h"
#define SST_EVT_POOL_NUM 2U
#define SST_MAX_TASK 8U
#define SST_LOG2(x) ((uint_fast8_t)(32U-__builtin_clz((unsigned)(x))))
typedef uint16_t SST_TaskPrio;
typedef unsigned SST_LockKey;
#define SST_PORT_TASK_ATTR \
    TaskHandle_t thread; StaticTask_t *tcb; StackType_t *stack; \
    configSTACK_DEPTH_TYPE stackSizeWords;
#define SST_PORT_TASK_OPER \
    void SST_Task_activate(SST_Task *me); \
    void SST_Task_setPrio(SST_Task *me,SST_TaskPrio prio);
#define SST_PORT_CRIT_STAT
#define SST_PORT_CRIT_ENTRY() taskENTER_CRITICAL()
#define SST_PORT_CRIT_EXIT() taskEXIT_CRITICAL()
#define SST_TASK_NOTIFY_INDEX 0U
#define SST_PORT_TASK_PEND() ((void)xTaskNotifyGiveIndexed(me->thread,0))
#include "sst_evt_pool.h"
void SST_onIdle(void);
