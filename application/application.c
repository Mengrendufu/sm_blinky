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
#include "asset.h"
#include "bsp.h"
#include "bsp_lcd.h"

//============================================================================
// Everything that happened before the scheduler reports through here, called
// from the first task that runs (Blinky_init). Bring-up cannot push records
// itself: a push before the scheduler enters a FreeRTOS critical section
// whose nesting counter still holds its sentinel, which leaves interrupts
// masked for the rest of the boot (see bsp.c).
void Application_bootReport(void) {
    BSP_bootReport();
    BSP_lcdBootReport();
    Asset_bootReport();
}
//............................................................................
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
