//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef SM_PORT_H_
#define SM_PORT_H_

// vendor ====================================================================
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f1xx_hal.h"
#include <stdbool.h>

// SM_Hsm port. ==============================================================
// #define SM_HSM_ROM  SNMTT_ROM
// #define SM_HSM_RETT SNMTT_REENTRANT

// Combination mode. .........................................................
#include <stddef.h> // size_t, offsetof
#ifndef container_of
    #define container_of(ptr_, type_, member_) \
                       ((type_ *)((char *)(ptr_) - offsetof(type_, member_)))
#endif // container_of

// SM_Tracer port. ===========================================================
// #define SMT_REET SNMTT_REENTRANT
#define SMT_CRITICAL_SECTION_ENTRY() taskENTER_CRITICAL()
#define SMT_CRITICAL_SECTION_EXIT()  taskEXIT_CRITICAL()
#define SM_TRACER_LEN 8

// SM_Assert port. ==========================================================
// #ifndef SM_ROM
//     #define SM_ROM SNMTT_ROM
// #endif // SM_ROM

//============================================================================
#include "sm_assert.h"
#include "sm_tracer.h"
#include "sm_hsm.h"

#endif // SM_PORT_H_
