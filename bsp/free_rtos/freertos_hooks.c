#include "FreeRTOS.h"
#include "task.h"
#include "sst.h"

// CPU load, sampled on the tick: how many of the last N ticks found the idle
// task on the CPU. The tick is 10 ms here (BSP_TICKS_PER_SEC 100), so a
// 500 ms window is fifty samples - a percentage to the nearest two. The cost
// is one pointer compare and two increments in an interrupt that already
// runs, and no timer of our own (DWT's cycle counter is out: it stops when
// the debugger is detached, see AGENTS.md). What it does not see: the
// tracer's pump runs inside the idle task, so pumping records reads as idle.
static volatile uint32_t BSP_cpuIdle_;
static volatile uint32_t BSP_cpuTicks_;

void vApplicationTickHook(void) {
    if (xTaskGetCurrentTaskHandle() == xTaskGetIdleTaskHandle()) {
        ++BSP_cpuIdle_;
    }
    ++BSP_cpuTicks_;
}
//............................................................................
// The two counters, for a caller that wants a load over its own window: it
// keeps the previous pair and divides the deltas (unsigned, so a wrap of the
// 32 bit counters takes care of itself).
//............................................................................
void BSP_cpuSample(uint32_t *idle, uint32_t *total) {
    *idle = BSP_cpuIdle_;
    *total = BSP_cpuTicks_;
}
//............................................................................
//============================================================================
// TODO(probe 2026-10-12): a known CPU load, so the corner's number can be
// checked against the glass. Duty walks 0/25/50/75/100 percent, three seconds
// each, for ever: the display should follow it (plus the ~6 percent the rest
// of the system uses). At the 100 percent step the idle task is starved, so
// the trace pump stops and nMin falls to 0 - that is the probe at work, not a
// fault. Remove this, its BSP_cpuProbeStart declaration and the main.c call
// once the meter is trusted.
static void BSP_cpuProbeTask_(void *arg) {
    TickType_t last = xTaskGetTickCount();
    TickType_t spin;
    uint32_t step = 0U;
    uint32_t n = 0U;

    (void)arg;
    for (;;) {
        spin = (TickType_t)(step % 5U);         // 0..4 ticks of burn
        while ((TickType_t)(xTaskGetTickCount() - last) < spin) {
            ;                                   // the load, and nothing else
        }
        vTaskDelay((TickType_t)(4U - spin));    // period = 4 ticks = 40 ms
        last = xTaskGetTickCount();
        if (++n >= 75U) {                       // 75 * 40 ms = 3 s per step
            n = 0U;
            ++step;
        }
    }
}
void BSP_cpuProbeStart(void) {
    static StaticTask_t tcb;
    static StackType_t stack[128];

    (void)xTaskCreateStatic(BSP_cpuProbeTask_, "cpuprobe", 128U, (void *)0,
                            tskIDLE_PRIORITY + 1U, stack, &tcb);
}
//............................................................................
void vApplicationIdleHook(void)
{
    SST_onIdle();
}

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize)
{
    static StaticTask_t idleTCB;
    static StackType_t idleStack[configMINIMAL_STACK_SIZE];

    *ppxIdleTaskTCBBuffer = &idleTCB;
    *ppxIdleTaskStackBuffer = idleStack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxTimerTaskStackBuffer,
                                    uint32_t *pulTimerTaskStackSize)
{
    static StaticTask_t timerTCB;
    static StackType_t timerStack[configTIMER_TASK_STACK_DEPTH];

    *ppxTimerTaskTCBBuffer = &timerTCB;
    *ppxTimerTaskStackBuffer = timerStack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    /* Project rule: losing the log is allowed, hanging is not. Report through
     * the assert path, which resets the board, so a too small static stack
     * shows up as a trace record plus a fresh boot instead of a dead
     * board. The task name goes out as the module string, so the record says
     * which task ran out instead of just that something did. */
    SM_onAssert(pcTaskName, 1);
}
