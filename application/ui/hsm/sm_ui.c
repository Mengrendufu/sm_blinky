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
#include "sm_assert.h"
#include "sm_ui.h"
#include "asset.h"
#include "ui.h"
#include "ui_evt_priv.h"
#include "widgets/widget_io_priv.h"
#include "bsp.h"
#include "sm_tracer.h"
#include "flash.h"
#include "aos/blinky/blinky.h"
#include "sst_port.h"   // defines SST_EVT_POOL_NUM, then the pool's API
#include "nvm.h"          // the snapshot both sides meet in

SM_DEFINE_MODULE("sm_ui")

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

// == DEBUG: whether a verified splash image was installed by the HSM. ==
volatile uint8_t Ui_dbgSplashOk_;

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

// The fixed BSP/UI timebase; state entries reset their local count.
#define SM_UI_SPLASH_BEATS \
    ((UI_SPLASH_MS * BSP_TICKS_PER_SEC + 999U) / 1000U)
#define SM_UI_PAGE_BEATS \
    ((UI_PAGE_MS * BSP_TICKS_PER_SEC + 999U) / 1000U)

// The live page's entry action publishes its edges, so the helper is declared
// ahead of it; its body sits further down beside the page actions it serves.
static void Sm_Ui_publish_(Sm_Ui * const self);

// == DEBUG: sustained single-widget refresh experiment. ==
// Request a two-percent step every 30 ms, cycling from empty to full and
// back to empty until the live page exits.
#define SM_UI_DIAL_MS 30U
#define SM_UI_DIAL_STEP 2U
#define SM_UI_DIAL_TOP 100U
#define SM_UI_DIAL_BOTTOM 0U

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
    "ui", "Flsh", "Blky", "tmr", "idle", "nmin", "imin", "smin"
};
static char const * const Sm_Ui_panelUnits_[SM_UI_PANEL_ROWS] = {
    "w", "w", "w", "w", "w", "-", "-", "-"
};
static char const * const Sm_Ui_stackNames_[SM_UI_TASK_ROWS] = {
    "ui", (char const *)0, (char const *)0, "Tmr Svc", "IDLE"
};

//============================================================================
static SM_StatePtr Sm_Ui_TOP_initial_(SM_Hsm * const me) SM_HSM_RETT;
static void Sm_Ui_splashing_entry_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Sm_Ui_splashing_(void * const me,
                                 void const * const e) SM_HSM_RETT;
static SM_StatePtr Sm_Ui_active_initial_(SM_Hsm * const me) SM_HSM_RETT;
static SM_RetState Sm_Ui_active_(void * const me,
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

// State-Splashing: show the boot image and wait for its display period.
// Timeout pulses only count the hold; active controls do not exist yet.
SM_HsmState SM_HSM_ROM Sm_Ui_splashing = {
    SM_HSM_TOP,           // super
    (SM_InitHandler)0,                // init_
    (SM_ActionHandler)&Sm_Ui_splashing_entry_,  // entry_
    (SM_ActionHandler)0,              // exit_
    (SM_StateHandler)&Sm_Ui_splashing_   // handler
};

// State-Active: Sys/Live update controls, then delegate the same timeout
// to this phase for its page-switch count and guard.
SM_HsmState SM_HSM_ROM Sm_Ui_active = {
    SM_HSM_TOP,                 // super
    (SM_InitHandler)&Sm_Ui_active_initial_, // init_
    (SM_ActionHandler)0,                   // entry_
    (SM_ActionHandler)0,                   // exit_
    (SM_StateHandler)&Sm_Ui_active_         // handler
};

// State-Sys: page 1, the system's own numbers - the CPU as a half gauge and
// one bar per thread and per pool. Its entry owns every widget on it.
SM_HsmState SM_HSM_ROM Sm_Ui_sys = {
    (SM_StatePtr)&Sm_Ui_active,       // super
    (SM_InitHandler)0,                // init_
    (SM_ActionHandler)&Sm_Ui_sys_entry_,     // entry_
    (SM_ActionHandler)&Sm_Ui_sys_exit_,      // exit_
    (SM_StateHandler)&Sm_Ui_sys_      // handler
};

// State-Live: page 2, the moving one - a spinner, a switch the AO's LED
// drives, and the CPU over the last twelve windows.
SM_HsmState SM_HSM_ROM Sm_Ui_live = {
    (SM_StatePtr)&Sm_Ui_active,       // super
    (SM_InitHandler)0,                // init_
    (SM_ActionHandler)&Sm_Ui_live_entry_,    // entry_
    (SM_ActionHandler)&Sm_Ui_live_exit_,     // exit_
    (SM_StateHandler)&Sm_Ui_live_     // handler
};

//============================================================================
// Page objects and borrowed asset descriptors belong to this HSM instance.
// Display registration, rendering and LCD completion stay in ui_disp.
static void Sm_Ui_pageInit_(Sm_Ui * const me) {
    lv_display_t * const display = lv_display_get_default();

    me->bgDsc = (lv_image_dsc_t){0};
    me->splashDsc = (lv_image_dsc_t){0};
    // The page container: one parent for every page's widgets, and the one
    // thing a page change empties. It sits below the chrome, which the shared
    // state builds once on the screen itself.
    me->pageRoot = lv_obj_create(lv_screen_active());
    lv_obj_set_size(me->pageRoot,
                     lv_display_get_horizontal_resolution(display),
                     lv_display_get_vertical_resolution(display));
    lv_obj_set_pos(me->pageRoot, 0, 0);
    lv_obj_set_style_border_width(me->pageRoot, 0, 0);
    lv_obj_set_style_radius(me->pageRoot, 0, 0);
    lv_obj_set_style_pad_all(me->pageRoot, 0, 0);
    lv_obj_set_style_shadow_width(me->pageRoot, 0, 0);
    lv_obj_remove_flag(me->pageRoot, LV_OBJ_FLAG_SCROLLABLE);
}

static void Sm_Ui_screenBase_(lv_obj_t * const screen) {
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000U), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
}

// One RGB565 descriptor per slot: 240x320, no copy, no decode - it points
// straight at the external SRAM. Every page shows one of these, so the panel
// has a single writer (the flush) and the state machine a single owner.
static void Sm_Ui_slotDesc_(lv_image_dsc_t * const dsc,
                            void const * const pixels) {
    lv_display_t * const display = lv_display_get_default();
    uint32_t const width =
        (uint32_t)lv_display_get_horizontal_resolution(display);

    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf = LV_COLOR_FORMAT_RGB565;
    dsc->header.w = width;
    dsc->header.h =
        (uint32_t)lv_display_get_vertical_resolution(display);
    dsc->header.stride = width * 2U;
    dsc->data_size = ASSET_SLOT_BYTES;
    dsc->data = (uint8_t const *)pixels;
}

//............................................................................
static void Sm_Ui_pageEnd_(Sm_Ui * const me) {
    lv_obj_set_style_bg_image_src(me->pageRoot, (void const *)0, 0);
    lv_obj_clean(me->pageRoot);
}
//............................................................................
// One page entry, whichever page it is: the last page's tree goes, then the
// background that page belongs to is laid down. The page's own state builds
// its widgets on top of it.
static void Sm_Ui_pageBegin_(Sm_Ui * const me, uint32_t slot) {
    Asset const * const asset = Asset_get();
    void const * const bgPixels = Asset_slot(asset, slot);

    Sm_Ui_pageEnd_(me);
    Sm_Ui_screenBase_(me->pageRoot);

    // The bootloader's asset, if it verified: the whole page becomes one
    // image
    // and the widgets sit on top of it. Without it the page keeps the black
    // background, which is the degradation the design counts on.
    if (bgPixels != (void const *)0) {
        Sm_Ui_slotDesc_(&me->bgDsc, bgPixels);
        lv_obj_set_style_bg_image_src(me->pageRoot, &me->bgDsc, 0);
    }

    me->display->requestFrame(me->display->context);
}

static bool Sm_Ui_splashView_(Sm_Ui * const me) {
    Asset const * const asset = Asset_get();
    void const * const pixels = Asset_slot(asset, 2U);

    Ui_dbgSplashOk_ = 0U;

    // The boot image is the page container's background, with the same
    // single flush writer as the content pages.
    Sm_Ui_pageEnd_(me);
    Sm_Ui_screenBase_(me->pageRoot);

    if (pixels == (void const *)0) {
        return false;   // No verified image is available to display.
    }

    Sm_Ui_slotDesc_(&me->splashDsc, pixels);

    lv_obj_set_style_bg_image_src(me->pageRoot, &me->splashDsc, 0);

    me->display->requestFrame(me->display->context);
    Ui_dbgSplashOk_ = 1U;

    return true;
}

// The chrome, built by the first content page and never taken down again: the
// topbar outlives every page, so a page change only rewrites its title. It is
// built late on purpose - while the boot image owns the glass there must be
// nothing of the pages' on it, and a topbar made earlier would sit over the
// image (the image is a child of the page container, which is created first).
static void Sm_Ui_chrome_(Sm_Ui * const me, char const *title) {
    if (me->top.title == (struct _lv_obj_t *)0) {
        W_Topbar_create(&me->top, lv_screen_active(), title);
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
        W_Topbar_setFps(&me->top,
            (unsigned)me->display->readFps(me->display->context));
        W_Topbar_setCpu(&me->top, (unsigned)me->cpuPct);
        me->statAt = now;
    }

    return slow;
}
//............................................................................
// One panel row per thread: the kernel supplies both total and free words.
// Both SST tasks are named "ao", so their rows match the owning AO handle;
// UI/timer/idle rows keep their unique-name lookup. Bars show free capacity.
static void Sm_Ui_panel_(Sm_Ui * const me) {
    static TaskStatus_t tasks[SM_UI_TASKS];
    configRUN_TIME_COUNTER_TYPE runTime;
    UBaseType_t const n = uxTaskGetSystemState(tasks, SM_UI_TASKS, &runTime);
    unsigned row;

    for (row = 0U; row < SM_UI_TASK_ROWS; ++row) {
        UBaseType_t i;

        for (i = 0U; i < n; ++i) {
            bool const matches = (row == 1U)
                ? (tasks[i].xHandle == AO_Flash->thread)
                : (row == 2U)
                  ? (tasks[i].xHandle == AO_Blinky->thread)
                  : (strcmp(tasks[i].pcTaskName,
                             Sm_Ui_stackNames_[row]) == 0);

            if (matches) {
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

    // The inbox records its lowest free-slot count on every enqueue, even
    // while Live is visible; display sampling cannot miss short queue peaks.
    {
        unsigned const len = Ui_evtInboxLen();
        unsigned const freeSlots = Ui_evtInboxMinFree();

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
void Sm_Ui_bindDisplay(Sm_Ui * const me,
                       Sm_Ui_DisplayOperations const *operations) {
    SM_REQUIRE(me != NULL);
    SM_REQUIRE(operations != NULL);
    SM_REQUIRE(operations->requestFrame != NULL);
    SM_REQUIRE(operations->setBacklight != NULL);
    SM_REQUIRE(operations->readFps != NULL);
    me->display = operations;
}
//............................................................................
void Sm_Ui_init(Sm_Ui * const me) {
    SM_Hsm_init_(&me->sm_hsm_, (SM_InitHandler)Sm_Ui_TOP_initial_);
}
//............................................................................
void Sm_Ui_dispatch(Sm_Ui * const me, SST_Evt const * const e) {
    SM_Hsm_dispatch_(&me->sm_hsm_, (void const *)e);
}
//............................................................................
// UML-Top-Initial
static SM_StatePtr Sm_Ui_TOP_initial_(SM_Hsm * const me) SM_HSM_RETT {
    Sm_Ui * const self = container_of((SM_Hsm *)me, Sm_Ui, sm_hsm_);

    SM_REQUIRE(self->display != NULL);
    self->ledOn = false;
    self->ledEdges = 0U;
    self->shownOn = false;
    self->shownEdges = 0U;
    self->shownValid = false;
    self->statAt = 0U;
    self->cpuIdle = 0U;
    self->cpuTotal = 0U;
    self->cpuPct = 0U;
    W_Topbar_init(&self->top);
    W_Gauge_init(&self->gauge);
    W_BarPanel_init(&self->panel);
    W_Dial_init(&self->dial);
    self->dialAt = 0U;
    self->dialPct = 0U;
    self->dialUp = true;
    W_Switch_init(&self->sw);
    W_Ram_init(&self->ram);
    Sm_Ui_pageInit_(self);
    return _SM_INIT(&Sm_Ui_splashing);
}
//............................................................................
// State-Splashing
static void Sm_Ui_splashing_entry_(SM_Hsm * const me) SM_HSM_RETT {
    Sm_Ui * const self = container_of((SM_Hsm *)me, Sm_Ui, sm_hsm_);

    self->display->setBacklight(self->display->context, true);
    // Without an image, the first timeout skips the splash hold.
    self->timeoutCount = Sm_Ui_splashView_(self)
                         ? 0U : SM_UI_SPLASH_BEATS;
}
static SM_RetState Sm_Ui_splashing_(void * const me,
                                    void const * const e) SM_HSM_RETT {
    Sm_Ui * const self = container_of((SM_Hsm *)me, Sm_Ui, sm_hsm_);
    SST_Evt const * const evt = (SST_Evt const *)e;

    switch (evt->sig) {
        case UI_TIMEOUT_SIG: {
            if (++self->timeoutCount >= SM_UI_SPLASH_BEATS) {
                return _SM_TRAN(&Sm_Ui_active);
            }
            return _SM_HANDLED();   // the boot image owns the glass
        }
        case UI_LED_ON_SIG:
        case UI_LED_OFF_SIG: {
            self->ledOn = (evt->sig == UI_LED_ON_SIG);
            ++self->ledEdges;
            return _SM_HANDLED();
        }
        default: {
            return _SM_SUPER();
        }
    }
}
//............................................................................
// State-Active
static SM_StatePtr Sm_Ui_active_initial_(SM_Hsm * const me) SM_HSM_RETT {
    (void)me;
    return _SM_INIT(&Sm_Ui_sys);
}

static SM_RetState Sm_Ui_active_(void * const me,
                                 void const * const e) SM_HSM_RETT {
    Sm_Ui * const self = container_of((SM_Hsm *)me, Sm_Ui, sm_hsm_);
    SST_Evt const * const evt = (SST_Evt const *)e;

    switch (evt->sig) {
        case UI_TIMEOUT_SIG: {
            if (++self->timeoutCount < SM_UI_PAGE_BEATS) {
                return _SM_HANDLED();
            }
            if (SM_Hsm_childState_(&self->sm_hsm_, &Sm_Ui_active)
                == &Sm_Ui_sys) {
                return _SM_TRAN(&Sm_Ui_live);
            }
            return _SM_TRAN(&Sm_Ui_sys);
        }
        case UI_LED_ON_SIG:
        case UI_LED_OFF_SIG: {
            self->ledOn = (evt->sig == UI_LED_ON_SIG);
            ++self->ledEdges;
            return _SM_HANDLED();
        }
        default: {
            return _SM_SUPER();
        }
    }
}
//............................................................................
// State-Sys (page 1)
static void Sm_Ui_sys_entry_(SM_Hsm * const me) SM_HSM_RETT {
    Sm_Ui * const self = container_of((SM_Hsm *)me, Sm_Ui, sm_hsm_);

    Sm_Ui_chrome_(self, "sys");
    Sm_Ui_pageBegin_(self, UI_BG_SLOT_SYS);
    W_Gauge_create(&self->gauge, self->pageRoot);
    W_BarPanel_create(&self->panel, self->pageRoot, Sm_Ui_panelNames_,
                      Sm_Ui_panelUnits_);
    W_Dots_create(&self->dots, self->pageRoot, 0U);
    self->shownValid = false;   // the model gets written out again
    self->statAt = 0U;          // slow readouts: rewrite next frame
    self->timeoutCount = 0U;
}
static void Sm_Ui_sys_exit_(SM_Hsm * const me) SM_HSM_RETT {
    Sm_Ui * const self = container_of((SM_Hsm *)me, Sm_Ui, sm_hsm_);

    Sm_Ui_pageEnd_(self);
}
static SM_RetState Sm_Ui_sys_(void * const me,
                              void const * const e) SM_HSM_RETT {
    Sm_Ui * const self = container_of((SM_Hsm *)me, Sm_Ui, sm_hsm_);
    SST_Evt const * const evt = (SST_Evt const *)e;

    switch (evt->sig) {
        case UI_TIMEOUT_SIG: {
            (void)Sm_Ui_fast_(self);
            if (Sm_Ui_slow_(self)) {
                Sm_Ui_panel_(self);
                W_Gauge_setValue(&self->gauge, (unsigned)self->cpuPct);
            }
            return _SM_SUPER(); // active owns the page-count guard.
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
    Sm_Ui_pageBegin_(self, UI_BG_SLOT_LIVE);
    // Stacked, never side by side: the switch used to sit inside the arc's
    // 96x96 box, and two boxes that overlap tear each other when one of them
    // is redrawn through a narrow area (2026-10-13, seen on the glass).
    W_Dial_create(&self->dial, self->pageRoot, 92, 40, 96);
    // Start the sweep empty, and let the first step come one period later:
    // the page always begins on an empty ring.
    self->dialPct = 0U;
    self->dialUp = true;
    self->dialAt = (uint32_t)xTaskGetTickCount()
                   + pdMS_TO_TICKS(SM_UI_DIAL_MS);
    W_Dial_setValue(&self->dial, 0U);
    // The switch rides beside the arc, centred on its middle: the two boxes
    // never touch (68 < 92), and the bottom row is then free for the label.
    W_Switch_create(&self->sw, self->pageRoot, 12, 74);
    W_Nvm_create(&self->nvm, self->pageRoot, 12, 181);
    // The snapshot, not the NOR: the flash task filled it at boot and writes
    // it back every ten seconds. Syncing here catches up on the edges counted
    // while other pages held the glass.
    Sm_Ui_publish_(self);
    self->nvmBlinky = Nvm_blinky();
    W_Nvm_set(&self->nvm, self->nvmBlinky);
    // The RAM figure never moves: it is the static image against the top of
    // the stack, so the page sets it once, on the way in.
    W_Ram_create(&self->ram, self->pageRoot);
    W_Ram_set(&self->ram,
              (uint32_t)((uintptr_t)&_ebss - SM_UI_RAM_BASE),
              (uint32_t)((uintptr_t)&_estack - SM_UI_RAM_BASE));
    W_Dots_create(&self->dots, self->pageRoot, 1U);
    self->shownValid = false;   // the led gets written out again
    self->statAt = 0U;          // the first window lands right away
    self->timeoutCount = 0U;
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
    Sm_Ui * const self = container_of((SM_Hsm *)me, Sm_Ui, sm_hsm_);

    /* The write to the NOR is the flash task's, on a deadline of its own:
     * every ten seconds, as the wear account in AGENTS.md covers. */
    Sm_Ui_pageEnd_(self);
}
static SM_RetState Sm_Ui_live_(void * const me,
                               void const * const e) SM_HSM_RETT {
    Sm_Ui * const self = container_of((SM_Hsm *)me, Sm_Ui, sm_hsm_);
    SST_Evt const * const evt = (SST_Evt const *)e;

    switch (evt->sig) {
        case UI_TIMEOUT_SIG: {
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
                else if (self->dialPct > SM_UI_DIAL_BOTTOM) {
                    self->dialPct -= SM_UI_DIAL_STEP;
                    if (self->dialPct <= SM_UI_DIAL_BOTTOM) {
                        self->dialUp = true;
                    }
                }
                self->dialAt = now + pdMS_TO_TICKS(SM_UI_DIAL_MS);
                W_Dial_setValue(&self->dial, self->dialPct);
            }
            return _SM_SUPER(); // active owns the page-count guard.
        }
        default: {
            return _SM_SUPER();
        }
    }
}
