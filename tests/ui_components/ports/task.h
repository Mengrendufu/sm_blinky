#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
int xTaskNotifyGiveIndexed(TaskHandle_t task,unsigned index);
TickType_t xTaskGetTickCount(void);

typedef struct {
    TaskHandle_t xHandle;
    char const *pcTaskName;
    StackType_t *pxEndOfStack;
    StackType_t *pxStackBase;
    unsigned usStackHighWaterMark;
} TaskStatus_t;
UBaseType_t uxTaskGetSystemState(TaskStatus_t *tasks,unsigned cap,
                                uint32_t *runtime);
TaskHandle_t xTaskCreateStatic(void (*entry)(void *),char const *name,
    unsigned words,void *arg,unsigned priority,StackType_t *stack,
    StaticTask_t *tcb);
unsigned ulTaskNotifyTakeIndexed(unsigned index,int clear,TickType_t wait);
