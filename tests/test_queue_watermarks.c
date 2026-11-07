#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "sst.h"

// Keep real FreeRTOS trace macros with host task/critical-section stubs.
#undef configASSERT
#include "../app/bsp/free_rtos/FreeRTOSConfig.h"

typedef int BaseType_t;
#define taskENTER_CRITICAL_FROM_ISR() (++Host_critical)
#define taskEXIT_CRITICAL_FROM_ISR(saved_) \
    do { (void)(saved_); taskEXIT_CRITICAL(); } while (0)
TaskHandle_t xTaskGetCurrentTaskHandle(void);
void vTaskNotifyGiveIndexedFromISR(TaskHandle_t task, unsigned index,
                                   BaseType_t *woken);

#include "../app/ports/sst_port.c"
#include "../app/bsp/free_rtos/freertos_hooks.c"

unsigned Host_critical;
SST_Task *SST_tasks_[SST_MAX_TASK + 1U];
static unsigned notifications;
static unsigned dispatched;

unsigned ulTaskNotifyTakeIndexed(unsigned index, int clear, TickType_t wait) {
    (void)index; (void)clear; (void)wait;
    abort();
}
void SM_onAssert(char const *module, int line) {
    fprintf(stderr, "ASSERT %s:%d\n", module, line);
    abort();
}

void DBC_fault_handler(char const *module, int line) {
    SM_onAssert(module, line);
}

TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (void *)1; }
TaskHandle_t xTaskGetIdleTaskHandle(void) { return (void *)2; }
TaskHandle_t xTaskCreateStatic(void (*entry)(void *), char const *name,
    unsigned words, void *arg, unsigned priority, StackType_t *stack,
    StaticTask_t *tcb) {
    assert(entry && name && words && arg && priority && stack && tcb);
    return (void *)1;
}
int xTaskNotifyGiveIndexed(TaskHandle_t task, unsigned index) {
    assert(Host_critical && task == (void *)1 && index == 0);
    ++notifications;
    return 1;
}
void vTaskNotifyGiveIndexedFromISR(TaskHandle_t task, unsigned index,
                                   BaseType_t *woken) {
    assert(task == (void *)1 && index == 0 && woken);
    ++notifications;
}

static void checkDispatch_(SST_Task *ao, SST_Evt const *event) {
    (void)ao;
    assert(!Host_critical && event->sig == 42);
    ++dispatched;
}

static void checkIsrLifetime_(void) {
    static STATIC_POOL_ELEM_TYPE(SST_Evt) storage[2];
    SST_EvtPool_init(storage, sizeof storage, sizeof(SST_Evt));
    SST_Evt const *aQueue[2] = {0}, *bQueue[2] = {0};
    SST_Task a = {
        .qBuf = aQueue, .end = 1, .thread = (void *)1,
        .queueMinFree = 2, .dispatch = checkDispatch_
    };
    SST_Task b = {
        .qBuf = bQueue, .end = 1, .thread = (void *)1,
        .queueMinFree = 2, .dispatch = checkDispatch_
    };
    BaseType_t woken = 0;
    SST_Evt const staticEvent = {.sig = 42};
    assert(SST_Task_postFromISR(&a, &staticEvent, &woken));
    SST_Task_activate(&a);
    assert(staticEvent.poolId == 0 && staticEvent.refCtr == 0);

    SST_Evt *event = SST_NEW(SST_Evt);
    event->sig = 42;
    assert(SST_Task_postFromISR(&a, event, &woken));
    assert(event->refCtr == 1);
    assert(SST_Task_postFromISR(&b, event, &woken));
    assert(event->refCtr == 2);
    SST_Task_activate(&a);
    assert(event->refCtr == 1 && b.nUsed == 1);
    SST_Evt *other = SST_NEW(SST_Evt);
    assert(other != event);
    SST_GC(other);
    SST_Task_activate(&b);

    SST_Evt *reused = SST_NEW(SST_Evt);
    assert(reused == event && reused->refCtr == 0);
    reused->sig = 42;
    assert(SST_Task_postFromISR(&a, reused, &woken));
    assert(reused->refCtr == 1);
    SST_Task_activate(&a);
    SST_Evt *all[] = {SST_NEW(SST_Evt), SST_NEW(SST_Evt)};
    assert(all[0] == reused && all[0] != all[1]);
    SST_GC(all[0]);
    SST_GC(all[1]);
    assert(dispatched == 4 && !a.nUsed && !b.nUsed && !Host_critical);
    puts("PASS ISR event lifetime: static, single/shared pooled events, "
         "last-consumer GC and full pool recovery");
}

static void checkBadIsrEvent_(unsigned mode) {
    assert(mode == 1 || mode == 2);
    SST_Evt const *queue[2] = {0};
    SST_Task ao = {
        .qBuf = queue, .end = 1, .thread = (void *)1, .queueMinFree = 2
    };
    SST_Evt event = {.sig = 42, .poolId = 1};
    if (mode == 1) {
        event.poolId = SST_EVT_POOL_NUM + 1U;
    }
    else {
        event.refCtr = UINT8_MAX;
    }
    BaseType_t woken = 0;
    (void)SST_Task_postFromISR(&ao, &event, &woken);
}

int main(int argc, char **argv) {
    if (argc == 2) {
        checkBadIsrEvent_((unsigned)atoi(argv[1]));
        return 0;
    }
    assert(argc == 1);
    SST_Evt const *queue[8] = {0};
    StackType_t stack[64];
    StaticTask_t tcb;
    SST_Task ao = {
        .qBuf = queue, .end = 7, .tcb = &tcb, .stack = stack,
        .stackSizeWords = 64
    };
    SST_Task_setPrio(&ao, (24U << 8) | 1U);
    assert(ao.queueMinFree == 8);
    // Record a task enqueue peak, then drain before the UI can sample.
    taskENTER_CRITICAL();
    ao.nUsed = 6;
    SST_Task_pend(&ao);
    ao.nUsed = 0;
    taskEXIT_CRITICAL();
    assert(ao.queueMinFree == 2);
    taskENTER_CRITICAL();
    ao.nUsed = 1;
    SST_Task_pend(&ao);
    taskEXIT_CRITICAL();
    assert(ao.queueMinFree == 2);
    // The ISR path must record full occupancy as well.
    SST_Evt const event = {.sig = 1};
    BaseType_t woken = 0;
    ao.nUsed = 7;
    assert(SST_Task_postFromISR(&ao, &event, &woken));
    ao.nUsed = 0;
    assert(ao.queueMinFree == 0 && notifications == 3);

    struct {
        uint32_t uxMessagesWaiting;
        uint32_t uxLength;
    } timer = {0, 10}, other = {0, 8};
    uint32_t minimum, total;
    assert(!BSP_timerQueueSample(&minimum, &total));
    traceQUEUE_REGISTRY_ADD(&other, "Other");
    traceQUEUE_SEND(&other);
    traceQUEUE_REGISTRY_ADD(&timer, "TmrQ");
    timer.uxMessagesWaiting = 6;
    traceQUEUE_SEND(&timer);
    timer.uxMessagesWaiting = 0;
    assert(BSP_timerQueueSample(&minimum, &total));
    assert(minimum == 3 && total == 10);
    timer.uxMessagesWaiting = 9;
    traceQUEUE_SEND_FROM_ISR(&timer);
    timer.uxMessagesWaiting = 0;
    traceQUEUE_SEND(&timer);
    assert(BSP_timerQueueSample(&minimum, &total));
    assert(minimum == 0 && !Host_critical);
    puts("PASS queue watermarks: task/ISR peaks survive draining; "
         "real Timer queue trace hooks exclude unrelated queues");
    checkIsrLifetime_();
}
