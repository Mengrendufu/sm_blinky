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
void BSP_init(void);
void BSP_ledOn(void);
void BSP_ledOff(void);

// Periodic maintenance record: remaining stack (in words, 1 word = 4 B) of the
// calling task, the idle task and the timer task. Meant to be called from an
// AO every few ticks, to size the static stacks from measurements.
void BSP_memReport(void);

#endif // BSP_H_
