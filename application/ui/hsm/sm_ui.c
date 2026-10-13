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
#include "widgets/widget_io_priv.h"
#include "bsp.h"
#include "sm_tracer.h"
#include "FreeRTOS.h"
#include "task.h"

//============================================================================
// Which background slot each page lays down. The burner writes three, in this
// order: 0 = the night dashboard, 1 = the grid, 2 = the logo, which is the
// splash page's own and is not a page background.
#define UI_BG_SLOT_DEMO  0U
#define UI_BG_SLOT_STATS 1U

// How often a page's slow readouts (frame time, stack, pool) are rewritten.
// The fast ones follow the AO's events instead, so 2 Hz is where the numbers
// stop being interesting and the redraws start being expensive.
#define UI_STAT_MS         500U
// The refresh period the frame readout is read against: a percentage that
// means "how much of a 30 fps budget this frame took".
#define UI_FRAME_BUDGET_US 33000U

//============================================================================
static SM_StatePtr Sm_Ui_TOP_initial_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Sm_Ui_ui_(void * const me,
                             void const * const e) SM_HSM_RETT;
static void Sm_Ui_splash_entry_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Sm_Ui_splash_(void * const me,
                                 void const * const e) SM_HSM_RETT;
static void Sm_Ui_demo_entry_(SM_Hsm * const me) SM_HSM_RETT;
static void Sm_Ui_demo_exit_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Sm_Ui_demo_(void * const me,
                               void const * const e) SM_HSM_RETT;
static void Sm_Ui_stats_entry_(SM_Hsm * const me) SM_HSM_RETT;
static void Sm_Ui_stats_exit_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Sm_Ui_stats_(void * const me,
                                void const * const e) SM_HSM_RETT;

static void Sm_Ui_chrome_(Sm_Ui * const me, char const *title);
static bool Sm_Ui_fast_(Sm_Ui * const me);
static bool Sm_Ui_slow_(Sm_Ui * const me);

// State-Ui: the level every page shares. The LED events land here, so the
// model keeps tracking the AO while the splash owns the glass - the demo
// page's first frame then draws the state as it is, not as it was when the
// widgets were built. Its entry owns the chrome: the topbar is built once,
// here, and no page change takes it away.
SM_HsmState SM_HSM_ROM Sm_Ui_ui = {
    SM_HSM_TOP,                       // super
    (SM_InitHandler)0,                // init_
    (SM_ActionHandler)0,              // entry_
    (SM_ActionHandler)0,              // exit_
    (SM_StateHandler)&Sm_Ui_ui_       // handler
};

// State-Splash: the boot image, pushed by the entry action and held for
// UI_SPLASH_MS. Nothing else may draw while this state is current - the
// display adapter is told the page, and the thread does not run LVGL at all.
SM_HsmState SM_HSM_ROM Sm_Ui_splash = {
    (SM_StatePtr)&Sm_Ui_ui,           // super
    (SM_InitHandler)0,                // init_
    (SM_ActionHandler)&Sm_Ui_splash_entry_,  // entry_
    (SM_ActionHandler)0,              // exit_
    (SM_StateHandler)&Sm_Ui_splash_   // handler
};

// State-Stats: the second content page. Same shape as Demo, different tree -
// and the two hand the glass back and forth on UI_PAGE_SWITCH_SIG.
SM_HsmState SM_HSM_ROM Sm_Ui_stats = {
    (SM_StatePtr)&Sm_Ui_ui,           // super
    (SM_InitHandler)0,                // init_
    (SM_ActionHandler)&Sm_Ui_stats_entry_,  // entry_
    (SM_ActionHandler)&Sm_Ui_stats_exit_,   // exit_
    (SM_StateHandler)&Sm_Ui_stats_    // handler
};

// State-Demo: the page the firmware lives in. Its entry asks the display
// adapter for a whole-screen repaint, which is where the first frame gets the
// splash's own treatment: cleared first, coverage proven.
SM_HsmState SM_HSM_ROM Sm_Ui_demo = {
    (SM_StatePtr)&Sm_Ui_ui,           // super
    (SM_InitHandler)0,                // init_
    (SM_ActionHandler)&Sm_Ui_demo_entry_,    // entry_
    (SM_ActionHandler)&Sm_Ui_demo_exit_,     // exit_
    (SM_StateHandler)&Sm_Ui_demo_     // handler
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
// The corner both pages carry: the led follows the model, and the CPU readout
// is the load over the window that just passed. The tick hook counts the
// ticks
// that found the idle task on the CPU, so everything else is work. At 10 ms a
// tick and a 500 ms window that is fifty samples - a percentage to the
// nearest
// two. Returns true when the model moved, which is what the pages' fast
// readouts ride on.
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
// Returns true when a new window was written out.
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
    W_Ring_init(&me->ring);
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

    // The page is the boot image: the display adapter clears the page, pushes
    // it and proves the push covered it. A push that could not be proven (no
    // asset, or a frame the panel would not take whole) leaves no hold:
    // holding a glass that is showing something else is the ghost.
    self->splashUp = Ui_dispSplash();
    self->splashUntil = xTaskGetTickCount() + pdMS_TO_TICKS(UI_SPLASH_MS);
}
static SM_RetState Sm_Ui_splash_(void * const me,
                                 void const * const e) SM_HSM_RETT {
    Ui_Evt const * const evt = (Ui_Evt const *)e;

    switch (evt->sig) {
        case UI_SPLASH_DONE_SIG: {
            (void)me;
            return _SM_TRAN(&Sm_Ui_demo);
        }
        case UI_FRAME_SIG: {
            // The boot image owns the glass: a frame here would be a widget
            // on
            // top of it.
            (void)me;
            return _SM_HANDLED();
        }
        default: {
            return _SM_SUPER();   // the LED events are the shared level's
        }
    }
}
//............................................................................
// State-Demo
static void Sm_Ui_demo_entry_(SM_Hsm * const me) SM_HSM_RETT {
    Sm_Ui * const self = container_of((SM_Hsm *)me, Sm_Ui, sm_hsm_);

    self->splashUp = false;
    Sm_Ui_chrome_(self, "sm_blinky");
    Ui_dispPageBegin(UI_BG_SLOT_DEMO);
    W_Ring_create(&self->ring, Ui_dispPageRoot());
    // The ring is the beat made visible, not the raw count: seven steps per
    // LED edge is about 17 degrees, which the eye catches, and a full turn
    // takes some seven seconds. The raw number stays on the card.
    W_Ring_setUnit(&self->ring, "beat");
    // No pair for the beat: the blink in the corner is that readout, and it
    // is
    // the one the eye gets for free. Card A held nothing else, so it went
    // with
    // its last label instead of standing there empty.
    W_Card_create(&self->cardB, Ui_dispPageRoot(), 124, 204, "edges",
                  "frame");
    W_Bar_create(&self->bar, Ui_dispPageRoot());
    W_Dots_create(&self->dots, Ui_dispPageRoot(), 0U);
    self->shownValid = false;   // the model gets written out again
    self->statAt = 0U;          // slow readouts: rewrite next frame
}
static void Sm_Ui_demo_exit_(SM_Hsm * const me) SM_HSM_RETT {
    (void)me;
    Ui_dispPageEnd();
}
static SM_RetState Sm_Ui_demo_(void * const me,
                               void const * const e) SM_HSM_RETT {
    Sm_Ui * const self = container_of((SM_Hsm *)me, Sm_Ui, sm_hsm_);
    Ui_Evt const * const evt = (Ui_Evt const *)e;

    switch (evt->sig) {
        case UI_PAGE_SWITCH_SIG: {
            return _SM_TRAN(&Sm_Ui_stats);
        }
        case UI_FRAME_SIG: {
            // The ring follows the AO's edge count, wrapped to one turn, and
            // the number in it spells the same value: a ring nobody can read
            // is decoration, and decoration is what the art already is. Seven
            // steps per edge, and not an lv_anim on purpose: an animated
            // value
            // would re-render the ring on every LVGL tick at about 21 ms a
            // frame, which saturates this thread. One update per LED event is
            // 4 percent of a 500 ms period.
            if (Sm_Ui_fast_(self)) {
                uint32_t const v = ((uint32_t)self->ledEdges * 7U) % 101U;

                W_Ring_setValue(&self->ring, (uint16_t)v);
                W_Card_setA(&self->cardB, "%u", (unsigned)self->ledEdges);
            }
            if (Sm_Ui_slow_(self)) {
                uint32_t const frameUs = Ui_dispFrameUs();

                W_Card_setBms(&self->cardB, frameUs);
                W_Bar_setValue(&self->bar,
                               WidgetIO_pct(frameUs, UI_FRAME_BUDGET_US));
            }
            return _SM_HANDLED();
        }
        default: {
            return _SM_SUPER();
        }
    }
}
//............................................................................
// State-Stats
static void Sm_Ui_stats_entry_(SM_Hsm * const me) SM_HSM_RETT {
    Sm_Ui * const self = container_of((SM_Hsm *)me, Sm_Ui, sm_hsm_);

    Sm_Ui_chrome_(self, "STATS");
    Ui_dispPageBegin(UI_BG_SLOT_STATS);
    W_Ring_create(&self->ring, Ui_dispPageRoot());
    // The pool's number gave up its slot to the tracer's watermark: the bar
    // along the bottom is the pool gauge, so the text was the duplicate.
    W_Ring_setUnit(&self->ring, "%");
    W_Card_create(&self->cardA, Ui_dispPageRoot(), 12, 204, "stack free",
                  "tracer nmin");
    W_Card_create(&self->cardB, Ui_dispPageRoot(), 124, 204, "frame min",
                  "frame max");
    W_Bar_create(&self->bar, Ui_dispPageRoot());
    W_Dots_create(&self->dots, Ui_dispPageRoot(), 1U);
    self->shownValid = false;   // the model gets written out again
    self->statAt = 0U;          // slow readouts: rewrite next frame
}
static void Sm_Ui_stats_exit_(SM_Hsm * const me) SM_HSM_RETT {
    (void)me;
    Ui_dispPageEnd();
}
static SM_RetState Sm_Ui_stats_(void * const me,
                                void const * const e) SM_HSM_RETT {
    Sm_Ui * const self = container_of((SM_Hsm *)me, Sm_Ui, sm_hsm_);
    Ui_Evt const * const evt = (Ui_Evt const *)e;

    switch (evt->sig) {
        case UI_PAGE_SWITCH_SIG: {
            return _SM_TRAN(&Sm_Ui_demo);
        }
        case UI_FRAME_SIG: {
            // The stats page is about what is left, so every readout on it is
            // a watermark: the stack's, the trace ring's, the pool's, and the
            // two frame times. The arc and the number inside the ring are the
            // same quantity now - one gauge, one number.
            (void)Sm_Ui_fast_(self);
            if (Sm_Ui_slow_(self)) {
                uint32_t const stackFree =
                    (uint32_t)uxTaskGetStackHighWaterMark((TaskHandle_t)0);
                uint32_t const stackPct =
                    WidgetIO_pct(UI_STACK_WORDS - stackFree, UI_STACK_WORDS);
                // The tracer's own high-water mark: the fewest free record
                // slots its ring has ever had. It only falls, and 0 means the
                // pump fell behind far enough to drop records. Reading the
                // byte is atomic on this core, and a read one window late
                // costs nothing.
                uint32_t const nMin = (uint32_t)SM_Tracer_inst.nMin;

                W_Ring_setValue(&self->ring, (uint16_t)stackPct);
                W_Card_setA(&self->cardA, "%u w", (unsigned)stackFree);
                W_Card_setB(&self->cardA, "%u", (unsigned)nMin);
                W_Card_setAms(&self->cardB, Ui_dispFrameMin());
                W_Card_setBms(&self->cardB, Ui_dispFrameMax());
                W_Bar_setValue(&self->bar, WidgetIO_poolUsedPct());
            }
            return _SM_HANDLED();
        }
        default: {
            return _SM_SUPER();
        }
    }
}
