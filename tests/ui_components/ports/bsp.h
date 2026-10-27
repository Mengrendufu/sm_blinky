#pragma once
#include <stdint.h>
#include "sm_port.h"
#define BSP_TICKS_PER_SEC 100U
void BSP_ledOn(void);
void BSP_ledOff(void);

void BSP_cpuSample(uint32_t *idle,uint32_t *total);
