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
#include "sst_port.h"   // defines SST_EVT_POOL_NUM, then the pool's API
#include "nvm.h"          // the snapshot both sides meet in

// The internal SRAM as the linker script laid it out: the static image ends
// at _ebss, the stack starts at the top (_estack). The gap between them is
// what is left, and it still holds the 1.5 KB the script reserves for the
// heap and the MSP, so "free" on the live page is the raw gap, not the
// usable remainder.
extern uint8_t _ebss;
extern uint8_t _estack;
#define SM_UI_RAM_BASE 0x20000000U
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

// The live page's entry action publishes its edges, so the helper is declared
// ahead of it; its body sits further down beside the page actions it serves.
static void Sm_Ui_publish_(Sm_Ui * const self);

// The live page's arc, one flourish per visit: empty to full, then back down
// to half, and that is where the page leaves it. The step is deliberately
// small - two percent reads as a turning ring, five read as jumps - so the
// period has to be short: fifty steps up and twenty five down at 100 ms is
// 7.5 seconds, inside UI_PAGE_MS. Each step is one whole-band redraw, about
// 5 ms, so the flourish costs about 5 percent of the thread while its page is
// up.
#define SM_UI_DIAL_MS 100U
#define SM_UI_DIAL_STEP 2U
#define SM_UI_DIAL_TOP 100U
#define SM_UI_DIAL_HOLD 50U

// The task rows of page 1's panel: five threads, then the two pools. Only the
// order and the names are ours - the sizes come from the kernel, which
// records
// each stack's high address (configRECORD_STACK_HIGH_ADDRESS) so a row's
// total
// is pxEndOfStack - pxStackBase + 1. Nothing to keep in step by hand.
#define SM_UI_TASK_ROWS 5U
#define SM_UI_PANEL_ROWS W_BARPANEL_ROWS
#define SM_UI_TASKS 8U

// Short names on purpose: a label that reaches into its neighbour's box is
// redrawn through a narrow area when the neighbour moves, and that tears.
static char const * const Sm_Ui_panelNames_[SM_UI_PANEL_ROWS] = {
    "ui", "flash", "ao", "tmr", "idle", "nmin", "inbox", "sst"
};
static char const * const Sm_Ui_panelUnits_[SM_UI_PANEL_ROWS] = {
    "w", "w", "w", "w", "w", "-", "-", "-"
};
static char const * const Sm_Ui_stackNames_[SM_UI_TASK_ROWS] = {
    "ui", "flash", "ao", "Tmr Svc", "IDLE"
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
                uint32_t const total =
                    (uint32_t)(tasks[i].pxEndOfStack - tasks[i].pxStackBase)
                    + 1U;
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
    // The SM (SST) static event pool, read the same way the tracer's ring is:
    // the fewest free blocks it has ever had, not the current count. The AO's
    // timeouts are static objects, so this stays at the full count until some
    // AO starts allocating events from the pool - which is exactly what the
    // row is there to show.
    {
        unsigned const total = SST_EvtPool_total();
        unsigned const minFree = SST_EvtPool_min();

        W_BarPanel_setPair(&me->panel, SM_UI_TASK_ROWS + 2U,
                           WidgetIO_pct(minFree, total), minFree, total);
    }
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
    me->dialUp = true;
    W_Switch_init(&me->sw);
    W_Ram_init(&me->ram);
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
    // Start the sweep empty, and let the first step come one period later:
    // the page always begins on an empty ring.
    self->dialPct = 0U;
    self->dialUp = true;
    self->dialAt = (uint32_t)xTaskGetTickCount()
                   + pdMS_TO_TICKS(SM_UI_DIAL_MS);
    W_Dial_setValue(&self->dial, 0U);
    W_Switch_create(&self->sw, Ui_dispPageRoot(), 12, 176);
    W_Nvm_create(&self->nvm, Ui_dispPageRoot(), 74, 181);
    // The snapshot, not the NOR: the flash task filled it at boot and writes
    // it back every ten seconds. Syncing here catches up on the edges counted
    // while other pages held the glass.
    Sm_Ui_publish_(self);
    self->nvmBlinky = Nvm_blinky();
    W_Nvm_set(&self->nvm, self->nvmBlinky);
    // The RAM figure never moves: it is the static image against the top of
    // the stack, so the page sets it once, on the way in.
    W_Ram_create(&self->ram, Ui_dispPageRoot());
    W_Ram_set(&self->ram,
              (uint32_t)((uintptr_t)&_ebss - SM_UI_RAM_BASE),
              (uint32_t)((uintptr_t)&_estack - SM_UI_RAM_BASE));
    W_Dots_create(&self->dots, Ui_dispPageRoot(), 1U);
    self->shownValid = false;   // the led gets written out again
    self->statAt = 0U;          // the first window lands right away
}
//............................................................................
// Hand the snapshot every edge counted since the last hand-off. The snapshot
// is cumulative - it was restored from the NOR at boot - so it takes a delta.
// Writing ledEdges out as it stands would put this boot's small number over
// the stored one, which is the bug this replaces (2026-10-13, seen on the
// glass as the count restarting near zero after a reset).
static void Sm_Ui_publish_(Sm_Ui * const self) {
    uint32_t const d = self->ledEdges - self->nvmPublished;

    if (d != 0U) {
        Nvm_addBlinky(d);
        self->nvmPublished = self->ledEdges;
    }
}
//............................................................................
static void Sm_Ui_live_exit_(SM_Hsm * const me) SM_HSM_RETT {
    (void)me;

    /* The write to the NOR is the flash task's, on a deadline of its own:
     * every ten seconds, as the wear account in AGENTS.md covers. */
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
                // Publish: the snapshot is what the flash task writes out,
                // so every edge the page sees goes into it.
                Sm_Ui_publish_(self);
            }
            (void)Sm_Ui_slow_(self);   // the corner's cpu readout

            // NVM_Blinky reads the snapshot: the number on the glass is the
            // count as it stands, and the flash task moves it to the NOR.
            {
                uint32_t const v2 = Nvm_blinky();

                if (v2 != self->nvmBlinky) {
                    self->nvmBlinky = v2;
                    W_Nvm_set(&self->nvm, v2);
                }
            }

            if ((int32_t)(now - self->dialAt) >= 0) {
                if (self->dialUp) {
                    self->dialPct += SM_UI_DIAL_STEP;
                    if (self->dialPct >= SM_UI_DIAL_TOP) {
                        self->dialPct = SM_UI_DIAL_TOP;
                        self->dialUp = false;
                    }
                }
                else if (self->dialPct > SM_UI_DIAL_HOLD) {
                    self->dialPct -= SM_UI_DIAL_STEP;
                }
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
