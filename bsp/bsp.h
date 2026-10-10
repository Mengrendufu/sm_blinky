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
// Trace record ids for the stack report below. Every caller passes its own,
// so a line says which task the numbers belong to: [MEM][STACK][<who>][<n>].
#define BSP_TRACE_STACK_BLINKY 21U
#define BSP_TRACE_STACK_IDLE   22U
#define BSP_TRACE_STACK_TIMER  23U
#define BSP_TRACE_STACK_UI     24U
// 37 belongs to the panel's frame memory check (bsp_lcd.c), so the flash
// thread's stack report takes 38.
#define BSP_TRACE_STACK_FLASH  38U
#define BSP_TRACE_STACK_FLASH_WT 44U

//============================================================================
void BSP_init(void);

// Builds the trace channel. Called first thing in BSP_init(): the constructor
// is plain RAM writes, so it is safe before the scheduler. What must wait for
// a task is *pushing* - SM_Trace_getBlock enters a FreeRTOS critical section,
// and before the scheduler that masks interrupts for the rest of the boot
// (see bsp.c). Records pushed before the first task would be dropped.
void BSP_Tracer_init(void);

// Boot phase timing: mark at each phase boundary in main(), report once the
// tracer is up. The marks themselves stay readable in RAM for a debugger.
void BSP_bootMark(void);
void BSP_bootReport(void);

// The fault frame the reset handler left in the cross reset area, or zeroed
// words when there was none (bsp/platform/stm32f1xx_it.c writes it).
uint32_t *BSP_faultStash(void);
void BSP_ledOn(void);
void BSP_ledOff(void);

// Periodic maintenance record: remaining stack (in words, 1 word = 4 B) of
// the calling task, the idle task and the timer task. Meant to be called from
// a task every few ticks, to size the static stacks from measurements.
void BSP_memReport(uint8_t selfRecId);

// Debug aid: write a short line straight out of the UART instead of through
// the tracer. The tracer is drained by the idle task, so a hang in a high
// priority task takes the whole log down with it, and a record pushed from
// here would never reach the wire. Use it with a *raw* terminal: the
// sm_tracer_tui parses frames, so plain text like this is dropped there.
void BSP_traceRaw(char const *text);

#endif // BSP_H_
