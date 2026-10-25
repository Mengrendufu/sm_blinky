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
#include "application_evt.h"
#include "sst_pubsub.h"
#include "bsp.h"
#include "bsp_lcd.h"
#include "flash.h"
#include "bsp_tick.h"
#include "ui.h"

//============================================================================
// Everything that happened before the scheduler reports through here, called
// from the Flash AO first bring-up event. Bring-up cannot push records
// itself: a push before the scheduler enters a FreeRTOS critical section
// whose nesting counter still holds its sentinel, which leaves interrupts
// masked for the rest of the boot (see bsp.c).
void Application_dbgBootReport(void) {
    BSP_dbgBootReport();
    BSP_dbgLcdBootReport();
}
//............................................................................
static void Application_sstTick_(void *context) {
    (void)context;
    SST_TimeEvt_tick();
}

void Application_tickStart(void) {
    static BSP_TickOperations const operations[] = {
        { .context = NULL, .tick = Application_sstTick_ },
        { .context = NULL, .tick = Ui_tick }
    };

    BSP_tickStart(operations, ARRAY_NELEM(operations));
}
//............................................................................
void AO_start(void) {
    //........................................................................
    // Register all subscriptions before the scheduler can publish.
    static SST_SubscrSet subscriptions[APPLICATION_PUB_SIG_MAX];
    SST_PubSub_init(subscriptions, APPLICATION_PUB_SIG_MAX);

    // AO_Flash: TOP init only queues its first bring-up event.
    Flash_ctor();
    static SST_Evt const *Flash_qSto[FLASH_QUEUE_LEN];
    SST_Task_start(AO_Flash,
                   (SST_TaskPrio)((23U << 8) | 2U),
                   Flash_qSto, ARRAY_NELEM(Flash_qSto),
                   (SST_Evt const *)0);

    // AO_Blinky...
    Blinky_ctor();
    static SST_Evt const * Blinky_qSto[8];
    /* priority: hi=FreeRTOS(24), lo=SST(1) */
    SST_Task_start(AO_Blinky,
                   (SST_TaskPrio)((24U << 8) | 1U),
                   Blinky_qSto, ARRAY_NELEM(Blinky_qSto),
                   (SST_Evt *)0);
}
