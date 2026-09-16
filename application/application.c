//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "sst.h"
#include "sst_port.h"
#include "static_pool.h"
#include "application.h"

//============================================================================
void AO_start(void) {
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
