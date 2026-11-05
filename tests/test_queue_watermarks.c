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

unsigned ulTaskNotifyTakeIndexed(unsigned index, int clear, TickType_t wait) {
    (void)index; (void)clear; (void)wait;
    abort();
}
void SST_Evt_gc(void *event) { (void)event; }

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

int main(void) {
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
}
