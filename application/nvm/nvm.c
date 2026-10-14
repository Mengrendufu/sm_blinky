//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software; it comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
//============================================================================
#include "nvm.h"

#include "FreeRTOS.h"
#include "task.h"

static volatile uint32_t Nvm_blinky_;

void Nvm_setBlinky(uint32_t value) {
    taskENTER_CRITICAL();
    Nvm_blinky_ = value;
    taskEXIT_CRITICAL();
}

void Nvm_addBlinky(uint32_t edges) {
    taskENTER_CRITICAL();
    Nvm_blinky_ += edges;
    taskEXIT_CRITICAL();
}

uint32_t Nvm_blinky(void) {
    uint32_t value;

    taskENTER_CRITICAL();
    value = Nvm_blinky_;
    taskEXIT_CRITICAL();

    return value;
}
