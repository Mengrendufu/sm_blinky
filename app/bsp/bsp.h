//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef BSP_H_
#define BSP_H_

//============================================================================
#include "sm_port.h"

//============================================================================
#define BSP_TICKS_PER_SEC 100U

// On-board LED (DS0). Configured once from main.c, toggled by the blinky AO.
#define BSP_LED_PORT GPIOB
#define BSP_LED_PIN  GPIO_PIN_5

//============================================================================
// Stack trace IDs 21..24, 38 and 44 are retired; never reuse.

//============================================================================
void BSP_init(void);

// Builds the trace channel. Called first thing in BSP_init(): the constructor
// is plain RAM writes, so it is safe before the scheduler. What must wait for
// a task is *pushing* - SM_Trace_getBlock enters a FreeRTOS critical section,
// and before the scheduler that masks interrupts for the rest of the boot
// (see bsp.c). Records pushed before the first task would be dropped.
void BSP_Tracer_init(void);

// Three main() marks bracket BSP bring-up and task setup; record 94 carries
// two uint16 big-endian millisecond deltas. Record 79 is retired.
// Marks remain readable in RAM; assets/NVS and first-frame time are excluded.
void BSP_dbgBootMark(void);
void BSP_dbgBootReport(void);

void BSP_ledOn(void);
void BSP_ledOff(void);

// CPU load as seen from the tick: the idle sample count and the tick count,
// to be differenced by the caller over its own window (see freertos_hooks.c).
void BSP_cpuSample(uint32_t *idle, uint32_t *total);


#endif // BSP_H_
