//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
// BSP software-timer tick host

#ifndef BSP_TICK_H_
#define BSP_TICK_H_

#include <stddef.h>

// Mounted callbacks run sequentially on the FreeRTOS timer daemon. They
// must return promptly: no waits, LVGL calls or state-machine dispatch.
typedef struct {
    void *context;
    void (*tick)(void *context);
} BSP_TickOperations;

// Call once, after interrupt-dependent bring-up and before the scheduler.
// The immutable operations array must remain alive for the whole run.
// Bind all destination inboxes before starting the scheduler.
void BSP_tickStart(BSP_TickOperations const *operations, size_t count);

#endif // BSP_TICK_H_
