#pragma once
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
typedef uint32_t TickType_t;
typedef unsigned UBaseType_t;
typedef unsigned StackType_t;
typedef unsigned StaticTask_t;
typedef unsigned configSTACK_DEPTH_TYPE;
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 2
#define configASSERT(x) assert(x)
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(x) (x)
extern unsigned Host_critical;
#define taskENTER_CRITICAL() (++Host_critical)
#define taskEXIT_CRITICAL() do { \
    assert(Host_critical); --Host_critical; \
} while(0)

#define configRUN_TIME_COUNTER_TYPE uint32_t
