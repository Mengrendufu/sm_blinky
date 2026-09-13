//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "sm_port.h"
#include "sst.h"
#include "sst_port.h"
#include "static_pool.h"
#include "application.h"

//============================================================================
void AO_start(void) {
    //........................................................................
    // SM_Trace initialization......
    struct SM_Tracer_Frame {
        void *sto[ (SM_TRACER_LEN-1) / sizeof(void*) + 1];
    };
    static struct SM_Tracer_Frame SM_Tracer_pSto[16];
    static void *SM_Tracer_qSto[16];
    SM_Tracer_ctor(
        &SM_Tracer_inst,
        (void *)SM_Tracer_pSto,
        sizeof(SM_Tracer_pSto),
        sizeof(SM_Tracer_pSto[0]),
        (void *)SM_Tracer_qSto,
        sizeof(SM_Tracer_qSto) / sizeof(SM_Tracer_qSto[0]));

    //........................................................................
    // AOs......
    // AO_Blinky...
    Blinky_ctor();
    static SST_Evt const * Blinky_qSto[8];
    /* priority: hi=FreeRTOS(24), lo=SST(1) */
    SST_Task_start(AO_Blinky,
                   (SST_TaskPrio)((24U << 8) | 1U),
                   Blinky_qSto, ARRAY_NELEM(Blinky_qSto),
                   (SST_Evt *)0);
}
