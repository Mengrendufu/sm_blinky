#pragma once
#include <stdint.h>
#include "sm_port.h"
#define BSP_TICKS_PER_SEC 100U
#define BSP_TRACE_STACK_BLINKY 21U
void BSP_ledOn(void);
void BSP_ledOff(void);
void BSP_dbgMemReport(unsigned id);

#define BSP_TRACE_STACK_UI 24U
void BSP_cpuSample(uint32_t *idle,uint32_t *total);
