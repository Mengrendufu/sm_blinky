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
#include "sm_ui.h"
#include "ui.h"
#include "ui_disp_priv.h"
#include "ui_evt_priv.h"
#include "widgets/widget_io_priv.h"
#include "bsp.h"
#include "sm_tracer.h"
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"

//============================================================================
// Which background slot each page lays down. The burner writes three, in this
// order: 0 = the night dashboard, 1 = the grid, 2 = the logo, which is the
// splash page's own and is not a page background.
#define UI_BG_SLOT_SYS  0U
#define UI_BG_SLOT_LIVE 1U

// How often a page's slow readouts are rewritten. The LED events are the fast
// ones; 2 Hz is where numbers stop being interesting and redraws stop being
// cheap.
#define UI_STAT_MS 500U

// How fast the live page's arc steps round. Each step redraws the ring's
// whole bounding box as full-width bands, about 5 ms, so five a second is
// roughly 2.5 percent of the thread; at 30 fps it would be 15.
#define SM_UI_DIAL_MS 200U

// The task rows of page 1's panel: five threads, then the two pools. The
// kernel reports only how much of a stack is still free
// (uxTaskGetSystemState -> usStackHighWaterMark), so the percentage needs the
// other half of the sum from here. If a stack size changes, this table
// changes
// with it: ui.c UI_STACK_WORDS (2048), flash.c FLASH_STACK_WORDS (256),
// blinky.c BLINKY_STACK_WORDS (256), configTIMER_TASK_STACK_DEPTH (256),
// configMINIMAL_STACK_SIZE (128).
#define SM_UI_TASK_ROWS 5U
#define SM_UI_PANEL_ROWS W_BARPANEL_ROWS
#define SM_UI_TASKS 8U

// Short names on purpose: a label that reaches into its neighbour's box is
// redrawn through a narrow area when the neighbour moves, and that tears.
static char const * const Sm_Ui_panelNames_[SM_UI_PANEL_ROWS] = {
    "ui", "flash", "ao", "tmr", "idle", "nmin", "inbox", "pool"
};
static char const * const Sm_Ui_panelUnits_[SM_UI_PANEL_ROWS] = {
    "w", "w", "w", "w", "w", "-", "-", "KB"
};
static char const * const Sm_Ui_stackNames_[SM_UI_TASK_ROWS] = {
    "ui", "flash", "ao", "Tmr Svc", "IDLE"
};
static uint16_t const Sm_Ui_stackWords_[SM_UI_TASK_ROWS] = {
    2048U, 256U, 256U, 256U, 128U
};

//============================================================================
static SM_StatePtr Sm_Ui_TOP_initial_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Sm_Ui_ui_(void * const me,
                             void const * const e) SM_HSM_RETT;
static void Sm_Ui_splash_entry_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Sm_Ui_splash_(void * const me,
                                 void const * const e) SM_HSM_RETT;
static void Sm_Ui_sys_entry_(SM_Hsm * const me) SM_HSM_RETT;
static void Sm_Ui_sys_exit_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Sm_Ui_sys_(void * const me,
                              void const * const e) SM_HSM_RETT;
static void Sm_Ui_live_entry_(SM_Hsm * const me) SM_HSM_RETT;
static void Sm_Ui_live_exit_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Sm_Ui_live_(void * const me,
                               void const * const e) SM_HSM_RETT;

static void Sm_Ui_chrome_(Sm_Ui * const me, char const *title);
static bool Sm_Ui_fast_(Sm_Ui * const me);
static bool Sm_Ui_slow_(Sm_Ui * const me);

// State-Ui: the level every page shares. The LED events land here, so the
// model keeps tracking the AO while the splash owns the glass.
SM_HsmState SM_HSM_ROM Sm_Ui_ui = {
    SM_HSM_TOP,                       // super
    (SM_InitHandler)0,                // init_
    (SM_ActionHandler)0,              // entry_
    (SM_ActionHandler)0,              // exit_
    (SM_StateHandler)&Sm_Ui_ui_       // handler
};

// State-Splash: the boot image, pushed by the entry action and held for
// UI_SPLASH_MS. Nothing else may draw while this state is current.
SM_HsmState SM_HSM_ROM Sm_Ui_splash = {
    (SM_StatePtr)&Sm_Ui_ui,           // super
    (SM_InitHandler)0,                // init_
    (SM_ActionHandler)&Sm_Ui_splash_entry_,  // entry_
    (SM_ActionHandler)0,              // exit_
    (SM_StateHandler)&Sm_Ui_splash_   // handler
};

// State-Sys: page 1, the system's own numbers - the CPU as a half gauge and
// one bar per thread and per pool. Its entry owns every widget on it.
SM_HsmState SM_HSM_ROM Sm_Ui_sys = {
    (SM_StatePtr)&Sm_Ui_ui,           // super
    (SM_InitHandler)0,                // init_
    (SM_ActionHandler)&Sm_Ui_sys_entry_,     // entry_
    (SM_ActionHandler)&Sm_Ui_sys_exit_,      // exit_
    (SM_StateHandler)&Sm_Ui_sys_      // handler
};

// State-Live: page 2, the moving one - a spinner, a switch the AO's LED
// drives, and the CPU over the last twelve windows.
SM_HsmState SM_HSM_ROM Sm_Ui_live = {
    (SM_StatePtr)&Sm_Ui_ui,           // super
    (SM_InitHandler)0,                // init_
    (SM_ActionHandler)&Sm_Ui_live_entry_,    // entry_
    (SM_ActionHandler)&Sm_Ui_live_exit_,     // exit_
    (SM_StateHandler)&Sm_Ui_live_     // handler
};

//============================================================================
// The chrome, built by the first content page and never taken down again: the
// topbar outlives every page, so a page change only rewrites its title. It is
// built late on purpose - while the boot image owns the glass there must be
// nothing of the pages' on it, and a topbar made earlier would sit over the
// image (the image is a child of the page container, which is created first).
static void Sm_Ui_chrome_(Sm_Ui * const me, char const *title) {
    if (me->top.title == (struct _lv_obj_t *)0) {
        W_Topbar_create(&me->top, Ui_dispRoot(), title);
    }
    else {
        W_Topbar_setTitle(&me->top, title);
    }
}
//............................................................................
// The corner both pages carry: the led follows the model. Returns true when
// the model moved.
static bool Sm_Ui_fast_(Sm_Ui * const me) {
    bool const changed = (!me->shownValid) || (me->ledOn != me->shownOn)
                         || (me->ledEdges != me->shownEdges);

    if (changed) {
        me->shownOn = me->ledOn;
        me->shownEdges = me->ledEdges;
        me->shownValid = true;
        W_Topbar_setLed(&me->top, me->ledOn);
    }

    return changed;
}
//............................................................................
// The load over the window that just passed: the tick hook counts the ticks
// that found the idle task on the CPU, so everything else is work. At 10 ms a
// tick and a 500 ms window that is fifty samples - a percentage to the
// nearest
// two. Returns true when a new window was written out.
static bool Sm_Ui_slow_(Sm_Ui * const me) {
    uint32_t const now = (uint32_t)xTaskGetTickCount();
    bool const slow = (me->statAt == 0U)
                      || ((int32_t)(now - me->statAt)
                          >= (int32_t)pdMS_TO_TICKS(UI_STAT_MS));

    if (slow) {
        uint32_t idle;
        uint32_t total;

        BSP_cpuSample(&idle, &total);
        uint32_t const dIdle = idle - me->cpuIdle;
        uint32_t const dTotal = total - me->cpuTotal;

        me->cpuIdle = idle;
        me->cpuTotal = total;
        if (dTotal > 0U) {
            me->cpuPct = 100U - ((dIdle * 100U) / dTotal);
        }
        W_Topbar_setCpu(&me->top, (unsigned)me->cpuPct);
        me->statAt = now;
    }

    return slow;
}
//............................................................................
// One panel row per thread: the kernel gives the free words, the table above
// gives the total, so the bar is what has been used. The rows are matched by
// name because that is the only handle uxTaskGetSystemState() reports.
static void Sm_Ui_panel_(Sm_Ui * const me) {
    static TaskStatus_t tasks[SM_UI_TASKS];
    configRUN_TIME_COUNTER_TYPE runTime;
    UBaseType_t const n = uxTaskGetSystemState(tasks, SM_UI_TASKS, &runTime);
    unsigned row;

    for (row = 0U; row < SM_UI_TASK_ROWS; ++row) {
        UBaseType_t i;

        for (i = 0U; i < n; ++i) {
            if (strcmp(tasks[i].pcTaskName, Sm_Ui_stackNames_[row]) == 0) {
                uint32_t const total = (uint32_t)Sm_Ui_stackWords_[row];
                uint32_t const freeWords =
                    (uint32_t)tasks[i].usStackHighWaterMark;
                // Fuel gauge: the bar and the number both say what is
                // left, so a full bar and a big number mean the same thing -
                // healthy. (The panel used to mix the two directions, which
                // made "0" unreadable: full bar, or nothing left?)
                W_BarPanel_setWords(&me->panel, row,
                                    WidgetIO_pct(freeWords, total),
                                    freeWords);
                break;
            }
        }
    }

    // The tracer's own high-water mark: the fewest free record slots its ring
    // has ever had. It only falls, and 0 means the pump fell behind far
    // enough to drop records.
    {
        uint32_t const total = (uint32_t)SM_Tracer_inst.nTot;
        uint32_t const freeSlots = (uint32_t)SM_Tracer_inst.nMin;

        W_BarPanel_setPair(&me->panel, SM_UI_TASK_ROWS,
                           WidgetIO_pct(freeSlots, total), freeSlots, total);
    }

    // The UI inbox, and the LVGL pool: both hand over their own numbers.
    {
        unsigned const len = Ui_evtInboxLen();
        unsigned const freeSlots = Ui_evtInboxFree();

        W_BarPanel_setPair(&me->panel, SM_UI_TASK_ROWS + 1U,
                           WidgetIO_pct(freeSlots, len), freeSlots, len);
    }
    W_BarPanel_setWords(&me->panel, SM_UI_TASK_ROWS + 2U,
                        WidgetIO_poolFreePct(), WidgetIO_poolFreeKb());
}
//............................................................................
//============================================================================
void Sm_Ui_init(Sm_Ui * const me) {
    me->ledOn = false;
    me->ledEdges = 0U;
    me->splashUp = false;
    me->splashUntil = 0U;
    me->shownOn = false;
    me->shownEdges = 0U;
    me->shownValid = false;
    me->statAt = 0U;
    me->cpuIdle = 0U;
    me->cpuTotal = 0U;
    me->cpuPct = 0U;
    W_Topbar_init(&me->top);
    W_Gauge_init(&me->gauge);
    W_BarPanel_init(&me->panel);
    W_Dial_init(&me->dial);
    me->dialAt = 0U;
    me->dialPct = 0U;
    W_Switch_init(&me->sw);
    W_History_init(&me->hist);
    SM_Hsm_init_(&me->sm_hsm_, (SM_InitHandler)Sm_Ui_TOP_initial_);
}
//............................................................................
void Sm_Ui_dispatch(Sm_Ui * const me, Ui_Evt const * const e) {
    SM_Hsm_dispatch_(&me->sm_hsm_, (void const *)e);
}
//............................................................................
uint32_t Sm_Ui_splashLeft(Sm_Ui const * const me) {
    int32_t const left = (int32_t)(me->splashUntil - xTaskGetTickCount());

    return ((me->splashUp) && (left > 0)) ? (uint32_t)left : 0U;
}
//............................................................................
// UML-Top-Initial
static SM_StatePtr Sm_Ui_TOP_initial_(SM_Hsm * const me) SM_HSM_RETT {
    (void)me;
    return _SM_INIT(&Sm_Ui_splash);
}
//............................................................................
// State-Ui
static SM_RetState Sm_Ui_ui_(void * const me,
                             void const * const e) SM_HSM_RETT {
    Sm_Ui * const self = container_of((SM_Hsm *)me, Sm_Ui, sm_hsm_);
    Ui_Evt const * const evt = (Ui_Evt const *)e;

    switch (evt->sig) {
        case UI_LED_ON_SIG: {
            self->ledOn = true;
            ++self->ledEdges;
            return _SM_HANDLED();
        }
        case UI_LED_OFF_SIG: {
            self->ledOn = false;
            ++self->ledEdges;
            return _SM_HANDLED();
        }
        default: {
            return _SM_SUPER();
        }
    }
}
//............................................................................
// State-Splash
static void Sm_Ui_splash_entry_(SM_Hsm * const me) SM_HSM_RETT {
    Sm_Ui * const self = container_of((SM_Hsm *)me, Sm_Ui, sm_hsm_);

    self->splashUp = Ui_dispSplash();
    self->splashUntil = xTaskGetTickCount() + pdMS_TO_TICKS(UI_SPLASH_MS);
}
static SM_RetState Sm_Ui_splash_(void * const me,
                                 void const * const e) SM_HSM_RETT {
    Ui_Evt const * const evt = (Ui_Evt const *)e;

    switch (evt->sig) {
        case UI_SPLASH_DONE_SIG: {
            (void)me;
            return _SM_TRAN(&Sm_Ui_sys);
        }
        case UI_FRAME_SIG: {
            (void)me;
            return _SM_HANDLED();   // the boot image owns the glass
        }
        default: {
            return _SM_SUPER();     // the LED events are the shared level's
        }
    }
}
//............................................................................
// State-Sys (page 1)
static void Sm_Ui_sys_entry_(SM_Hsm * const me) SM_HSM_RETT {
    Sm_Ui * const self = container_of((SM_Hsm *)me, Sm_Ui, sm_hsm_);

    self->splashUp = false;
    Sm_Ui_chrome_(self, "sys");
    Ui_dispPageBegin(UI_BG_SLOT_SYS);
    W_Gauge_create(&self->gauge, Ui_dispPageRoot());
    W_BarPanel_create(&self->panel, Ui_dispPageRoot(), Sm_Ui_panelNames_,
                      Sm_Ui_panelUnits_);
    W_Dots_create(&self->dots, Ui_dispPageRoot(), 0U);
    self->shownValid = false;   // the model gets written out again
    self->statAt = 0U;          // slow readouts: rewrite next frame
}
static void Sm_Ui_sys_exit_(SM_Hsm * const me) SM_HSM_RETT {
    (void)me;
    Ui_dispPageEnd();
}
static SM_RetState Sm_Ui_sys_(void * const me,
                              void const * const e) SM_HSM_RETT {
    Sm_Ui * const self = container_of((SM_Hsm *)me, Sm_Ui, sm_hsm_);
    Ui_Evt const * const evt = (Ui_Evt const *)e;

    switch (evt->sig) {
        case UI_PAGE_SWITCH_SIG: {
            return _SM_TRAN(&Sm_Ui_live);
        }
        case UI_FRAME_SIG: {
            (void)Sm_Ui_fast_(self);
            if (Sm_Ui_slow_(self)) {
                Sm_Ui_panel_(self);
                W_Gauge_setValue(&self->gauge, (unsigned)self->cpuPct);
            }
            return _SM_HANDLED();
        }
        default: {
            return _SM_SUPER();
        }
    }
}
//............................................................................
// State-Live (page 2)
static void Sm_Ui_live_entry_(SM_Hsm * const me) SM_HSM_RETT {
    Sm_Ui * const self = container_of((SM_Hsm *)me, Sm_Ui, sm_hsm_);

    Sm_Ui_chrome_(self, "live");
    Ui_dispPageBegin(UI_BG_SLOT_LIVE);
    // Stacked, never side by side: the switch used to sit inside the arc's
    // 96x96 box, and two boxes that overlap tear each other when one of them
    // is redrawn through a narrow area (2026-10-13, seen on the glass).
    W_Dial_create(&self->dial, Ui_dispPageRoot(), 72, 40, 96);
    W_Switch_create(&self->sw, Ui_dispPageRoot(), 92, 146);
    W_History_create(&self->hist, Ui_dispPageRoot());
    W_Dots_create(&self->dots, Ui_dispPageRoot(), 1U);
    self->shownValid = false;   // the led gets written out again
    self->statAt = 0U;          // the first window lands right away
}
static void Sm_Ui_live_exit_(SM_Hsm * const me) SM_HSM_RETT {
    (void)me;
    Ui_dispPageEnd();
}
static SM_RetState Sm_Ui_live_(void * const me,
                               void const * const e) SM_HSM_RETT {
    Sm_Ui * const self = container_of((SM_Hsm *)me, Sm_Ui, sm_hsm_);
    Ui_Evt const * const evt = (Ui_Evt const *)e;

    switch (evt->sig) {
        case UI_PAGE_SWITCH_SIG: {
            return _SM_TRAN(&Sm_Ui_sys);
        }
        case UI_FRAME_SIG: {
            // The switch is the board's own beat, made visible: it flips when
            // the AO's LED does. The history is one bar per slow window, and
            // the dial keeps a clock of its own.
            uint32_t const now = (uint32_t)xTaskGetTickCount();
            if (Sm_Ui_fast_(self)) {
                W_Switch_set(&self->sw, self->ledOn);
            }
            if (Sm_Ui_slow_(self)) {
                W_History_push(&self->hist, (unsigned)self->cpuPct);
            }
            if ((int32_t)(now - self->dialAt) >= 0) {
                self->dialPct = (uint16_t)((self->dialPct + 7U) % 101U);
                self->dialAt = now + pdMS_TO_TICKS(SM_UI_DIAL_MS);
                W_Dial_setValue(&self->dial, self->dialPct);
            }
            return _SM_HANDLED();
        }
        default: {
            return _SM_SUPER();
        }
    }
}
