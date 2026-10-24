//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "bsp_tick.h"
#include "bsp.h"
#include "FreeRTOS.h"
#include "timers.h"

static StaticTimer_t BSP_tickStorage_;
static TimerHandle_t BSP_tickTimer_;
static BSP_TickOperations const *BSP_tickOperations_;
static size_t BSP_tickCount_;

static void BSP_tick_(TimerHandle_t timer) {
    (void)timer;
    for (size_t i = 0U; i < BSP_tickCount_; ++i) {
        BSP_tickOperations_[i].tick(BSP_tickOperations_[i].context);
    }
}

void BSP_tickStart(BSP_TickOperations const *operations, size_t count) {
    configASSERT(BSP_tickTimer_ == NULL);
    configASSERT(operations != NULL && count > 0U);
    configASSERT(configTICK_RATE_HZ / BSP_TICKS_PER_SEC > 0U);
    for (size_t i = 0U; i < count; ++i) {
        configASSERT(operations[i].tick != NULL);
    }
    BSP_tickOperations_ = operations;
    BSP_tickCount_ = count;
    BSP_tickTimer_ = xTimerCreateStatic("bspTick",
        configTICK_RATE_HZ / BSP_TICKS_PER_SEC, pdTRUE, NULL,
        BSP_tick_, &BSP_tickStorage_);
    configASSERT(BSP_tickTimer_ != NULL);
    configASSERT(xTimerStart(BSP_tickTimer_, 0U) == pdPASS);
}
