//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
//============================================================================
//=== Display adapter -- LVGL on the UI thread
//
// One partial buffer, one flush path: LVGL renders a strip into .bss, the
// flush callback opens the panel window and hands the strip to the panel
// DMA channel. The buffer is 15 KB of internal SRAM (charter §6).
//
// The wait is the block charter §7 asks for: the transfer's interrupt wakes
// this thread, so while the panel is being fed the CPU goes to the idle task
// and the tracer's pump keeps running.
//
// The one thing the next reader must not break is the destination address: it
// stays fixed (MINC off), because a destination that walks flips the panel's
// RS every 1024 pixels and turns the rest of the strip into commands. That is
// what "the screen came apart and a reset did not help" was - see charter §11
// for the whole post mortem, including the several wrong answers along the
// way.

#include "asset.h"
#include "bsp.h"
#include "bsp_lcd.h"
#include "sm_tracer.h"
#include "ui_disp_priv.h"
#include "widgets/w_bar.h"
#include "widgets/w_card.h"
#include "widgets/w_dots.h"
#include "widgets/w_ring.h"
#include "widgets/w_topbar.h"
#include "widgets/widget_io_priv.h"
#include "lvgl.h"
#include "FreeRTOS.h"
#include "task.h"

//============================================================================
#define UI_LCD_HOR 240
#define UI_LCD_VER 320

// 240 x 32 pixels x 2 B = 15,360 B, the single partial buffer of charter §6.
#define UI_STRIP_ROWS 32U
#define UI_STRIP_PX   (UI_LCD_HOR * UI_STRIP_ROWS)

// The longest the UI thread may sleep without checking LVGL. Used when LVGL
// has no timer pending, and as the ceiling for what it does ask for.
#define UI_FALLBACK_MS 10U

// How long the flush wait may spin before it calls the channel wedged: a
// 240x32 strip is 7,680 pixels, about 1.3 ms at the measured 169 ns/px, which
// leaves this bound two orders of magnitude of headroom.
#define UI_DMA_SPIN_LIMIT 2000000U

// The notification slot the flush waits on. Slot 0 wakes the loop for the
// inbox (ui.c); slot 1 is the "the blocking call you submitted has finished"
// slot from AGENTS.md, shared with the flash RPC - this thread blocks on one
// thing at a time, so one slot serves both.
#define UI_DMA_NOTIFY 1U

// A 240x32 strip is 7,680 pixels, about 0.9 ms of bus time at the measured
// 112 ns/px. The timeout catches a wedged channel, not a slow one.
#define UI_DMA_TIMEOUT_MS 20U

// The panel's *display* path is not part of the proof the pages are drawn
// under: it comes up on its own schedule (measured 3 to 8 s on a cold start,
// worst case past 11), and until it does the glass holds whatever was last
// driven into it, so a page change in that window can show as two pictures at
// once - the new page where the panel is already driving, the previous one
// elsewhere. Nothing here re-drives the screen over that window: the clear,
// its coverage proof and the asset being current are the whole of it. Under
// test (2026-10-12): the re-drive window was dropped, the glass is the judge.

// Which background slot each page lays down. The burner writes three, in
// this order: 0 = bg-a (夜色仪表), 1 = bg-b (网格科技), 2 = bg-logo, which is
// the splash page's own and is not a page background.
#define UI_BG_SLOT_DEMO  0U
#define UI_BG_SLOT_STATS 1U

// How often a page's slow readouts (frame time, stack, pool) are rewritten.
// The fast ones follow the AO's events instead, so 2 Hz is where the numbers
// stop being interesting and the redraws start being expensive.
#define UI_STAT_MS        500U
// The refresh period the frame readout is read against: a percentage that
// means "how much of a 30 fps budget this frame took".
#define UI_FRAME_BUDGET_US 33000U
#define UI_STACK_WORDS    2048U   // the UI task's static stack, in words
// How often the panel's inversion bit is re-sent. The clobber has no
// schedule, so this one never stops - it is one command, and it is the only
// panel register the run-time path touches (see BSP_lcdReassert).
#define UI_INVON_MS       100U

// The two content pages. The widgets are the same kinds on both, so one set
// of handles serves whichever page is up (see Ui_pageBegin_).

typedef enum {
    UI_PAGE_DEMO = 0,
    UI_PAGE_STATS
} Ui_Page;

static lv_display_t *Ui_disp_;
static lv_image_dsc_t Ui_bgDsc_;      // the background of the page that is up
static lv_image_dsc_t Ui_splashDsc_;  // boot image, slot 2
static Ui_Page Ui_page_;              // which page owns the glass
// The widgets own their own handles now (application/ui/widgets). These are
// the instances the page that is up was built from, and they are the only
// things the render writes through.
static struct W_Topbar Ui_top_;       // title, cpu readout, heartbeat led
static struct W_Ring Ui_ring_;        // the ring the art leaves room for
static struct W_Card Ui_cardA_;       // card at (12,204)
static struct W_Card Ui_cardB_;       // card at (124,204)
static struct W_Bar Ui_bar_;          // the bottom bar
static struct W_Dots Ui_dots_;        // the two page dots
// One heartbeat's cost, measured, and the extremes it has shown: the stats
// page reads them, so they are a readout and not a probe.
static uint32_t Ui_frameUs_;
static uint32_t Ui_frameMin_;
static uint32_t Ui_frameMax_;
static uint32_t Ui_statAt_;           // when the slow readouts were rewritten
// CPU load over the last window: the tick hook's two counters, differenced
// here (see BSP_cpuSample), and the percentage they come to.
static uint32_t Ui_cpuIdle_;
static uint32_t Ui_cpuTotal_;
static uint32_t Ui_cpuPct_;

// What the widgets currently show. Keeping it here means an unchanged state
// costs nothing: no property writes, no invalidation, no redraw.
static bool Ui_shownOn_;
static uint16_t Ui_shownEdges_;
static bool Ui_shownValid_;

static uint16_t Ui_strip_[UI_STRIP_PX] LV_ATTRIBUTE_MEM_ALIGN;

// Captured at bring-up: the thread the transfer's interrupt wakes.
static TaskHandle_t Ui_waiter_;

// A page's entry action sets this, and the first frame that answers it
// clears it: it asks for one whole-screen repaint, and it is what holds the
// glass dark until a whole picture is on it. There is no coverage proof
// around that frame any more - the sentinel is a bring-up instrument now
// (bsp_lcd.h) and the panel's writability was proven before the light came
// on.
static bool Ui_framePending_;
// The glass stays dark until the first whole frame is on the frame memory:
// bring-up leaves the backlight off (BSP_lcdInit) and this is where it comes
// on. The same schedule keeps bring-up's sentinel off the user's eye.
static bool Ui_backlightOn_;
// The widget tree is built once, by the demo page's entry action - never
// earlier. While the splash holds the glass there is nothing to draw, so a
// flag and a state disagreeing can no longer put widget pixels on the panel.
static bool Ui_built_;

// == DEBUG: whether the boot image holds the glass; read over J-Link. ==
volatile uint8_t Ui_dbgSplashOk_;
//============================================================================
static void Ui_buildDemo_(void);
static void Ui_buildStats_(void);
static void Ui_pageBegin_(uint32_t slot, Ui_Page page);
static void Ui_flush_(lv_display_t *disp, const lv_area_t *area,
                      uint8_t *px);
static void Ui_flushDone_(void);
static void Ui_flushWait_(lv_display_t *disp);
static void Ui_dmaWait_(uint32_t timeoutMs);
static uint32_t Ui_tick_(void);
static uint32_t Ui_us_(void);

//============================================================================
void Ui_dispInit(void) {
    lv_init();
    lv_tick_set_cb(Ui_tick_);

    Ui_disp_ = lv_display_create(UI_LCD_HOR, UI_LCD_VER);
    configASSERT(Ui_disp_ != (lv_display_t *)0);

    // The completion arrives on the channel's interrupt and the wait is this
    // thread's second notification slot. Wiring the wait callback keeps LVGL
    // from falling back to its own busy wait on disp->flushing
    // (lv_refr.c:1458), which charter §4.8 forbids.
    Ui_waiter_ = xTaskGetCurrentTaskHandle();
    BSP_lcdDmaSetDoneHook(Ui_flushDone_);
    lv_display_set_flush_wait_cb(Ui_disp_, Ui_flushWait_);

    lv_display_set_flush_cb(Ui_disp_, Ui_flush_);
    lv_display_set_buffers(Ui_disp_, Ui_strip_, (void *)0, sizeof(Ui_strip_),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    // No widgets here on purpose: the splash page owns the glass first, and
    // its entry action is what brings the demo page's tree into being.
}
//............................................................................
// Push the boot image and prove it: the frame is cleared first (and the clear
// proven), the coverage sentinel goes down, the whole 240x320 goes out as one
// DMA transfer straight from the external SRAM, and the sentinel has to be
// gone afterwards. False means the glass is not showing the boot image, and
// the caller must not hold it as if it were.
//
// The transfer is waited out as this thread's own wait - the flush's slot -
// because the splash runs on the UI thread now instead of before the
// scheduler: a poll here would spin a task that has an idle task to give the
// CPU to.
// One screen base for every page: no scrolling, and a real background so a
// widget that moves repaints what it covered. Black, because that is the
// colour BSP_lcdInit() leaves in the frame memory.
static void Ui_screenBase_(lv_obj_t * const screen) {
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000U), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
}

// One RGB565 descriptor per slot: 240x320, no copy, no decode - it points
// straight at the external SRAM. Every page shows one of these, so the panel
// has a single writer (the flush) and the state machine a single owner.
static void Ui_slotDesc_(lv_image_dsc_t * const dsc,
                         void const * const pixels) {
    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf = LV_COLOR_FORMAT_RGB565;
    dsc->header.w = UI_LCD_HOR;
    dsc->header.h = UI_LCD_VER;
    dsc->header.stride = UI_LCD_HOR * 2U;
    dsc->data_size = ASSET_SLOT_BYTES;
    dsc->data = (uint8_t const *)pixels;
}

bool Ui_dispSplash(void) {
    Asset const * const asset = Asset_get();
    void const * const pixels = Asset_slot(asset, 2U);
    lv_obj_t * const screen = lv_screen_active();
    lv_obj_t *img;

    Ui_dbgSplashOk_ = 0U;

    // The boot image is an ordinary widget now, not a push straight to the
    // panel: one writer on the glass, and the state machine owns it. The
    // frame's coverage proof still runs, in the heartbeat, on the first frame
    // this page draws.
    lv_obj_clean(screen);
    Ui_screenBase_(screen);

    if (pixels == (void const *)0) {
        return false;   // no asset: the demo page comes up straight away
    }

    Ui_slotDesc_(&Ui_splashDsc_, pixels);

    img = lv_image_create(screen);
    lv_image_set_src(img, &Ui_splashDsc_);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);

    Ui_framePending_ = true;   // this page starts with a whole screen
    Ui_dbgSplashOk_ = 1U;

    return true;
}
//............................................................................
// One page entry, whichever page it is: clean the screen, lay the background
// that page belongs to, hand the glass over, then let the page build its
// tree. Nothing else draws while this runs - which is why it is an entry
// action and not work the loop gets to later.
static void Ui_pageBegin_(uint32_t slot, Ui_Page page) {
    Asset const * const asset = Asset_get();
    void const * const bgPixels = Asset_slot(asset, slot);
    lv_obj_t * const screen = lv_screen_active();

    // Nothing to stop by hand: LVGL clears an object's animations in its own
    // delete path (lv_obj.c), and the led in the corner no longer animates.
    // The clean below is what takes the old page's widgets away.
    // Every page builds its own tree, from a clean screen: the page before it
    // is gone before this one exists, so the two can never share the glass.
    lv_obj_clean(screen);
    Ui_screenBase_(screen);

    // The bootloader's asset, if it verified: the whole screen becomes one
    // image and the widgets sit on top of it. Without it the page keeps the
    // black background, which is the degradation the design counts on.
    if (bgPixels != (void const *)0) {
        lv_obj_t *bg;

        Ui_slotDesc_(&Ui_bgDsc_, bgPixels);

        bg = lv_image_create(screen);
        lv_image_set_src(bg, &Ui_bgDsc_);
        lv_obj_align(bg, LV_ALIGN_CENTER, 0, 0);
        lv_obj_remove_flag(bg, LV_OBJ_FLAG_CLICKABLE);
    }

    Ui_page_ = page;
    Ui_built_ = true;
    Ui_framePending_ = true;   // this page starts with a whole screen
    Ui_shownValid_ = false;    // and the model gets written out again
    Ui_statAt_ = 0U;           // slow readouts: rewrite next frame
}

void Ui_dispDemo(void) {
    Ui_pageBegin_(UI_BG_SLOT_DEMO, UI_PAGE_DEMO);
    Ui_buildDemo_();
}
//............................................................................
void Ui_dispStats(void) {
    Ui_pageBegin_(UI_BG_SLOT_STATS, UI_PAGE_STATS);
    Ui_buildStats_();
}
//............................................................................
TickType_t Ui_dispHeartbeat(void) {
    static TickType_t Ui_invonAt_;
    uint32_t const t0 = Ui_us_();
    TickType_t const now = xTaskGetTickCount();

    // The panel's polarity, forever. Losing it means the glass comes up
    // inverted and every region drawn after that keeps the wrong polarity:
    // the picture goes bright-on-dark where it was dark-on-bright, and every
    // change leaves garbage. Measured on the glass (2026-10-12).
    if ((Ui_invonAt_ == 0U) || ((int32_t)(now - Ui_invonAt_) >= 0)) {
        BSP_lcdReassert();
        Ui_invonAt_ = now + pdMS_TO_TICKS(UI_INVON_MS);
    }

    // LVGL runs on every page now, the boot image included: it is a widget
    // like any other, and the page that owns the glass is the only page whose
    // tree exists (each entry action rebuilds from a clean screen). There is
    // nothing to hold back.

    // A page's first frame is a whole screen, so ask for the whole screen
    // once, out loud, instead of trusting the invalidations the entry action
    // happened to leave behind. Nothing is proven about it: see the flag's
    // own note above.
    if (Ui_framePending_) {
        lv_obj_invalidate(lv_screen_active());
    }

    uint32_t const next = lv_timer_handler();
    uint32_t const dt = Ui_us_() - t0;   // what one heartbeat really cost

    if ((Ui_frameMin_ == 0U) || (dt < Ui_frameMin_)) {
        Ui_frameMin_ = dt;
    }
    if (dt > Ui_frameMax_) {
        Ui_frameMax_ = dt;
    }
    Ui_frameUs_ = dt;

    if ((next == LV_NO_TIMER_READY) || (next > UI_FALLBACK_MS)) {
        return pdMS_TO_TICKS(UI_FALLBACK_MS);
    }

    return pdMS_TO_TICKS((next == 0U) ? 1U : next);
}
//............................................................................
// Which page is up, and what the widgets on it show. The two pages carry the
// same widget kinds, so this is one function with a half each rather than two
// entry points: Ui_page_ picks the half. The fast values follow the model,
// the slow ones are rewritten on UI_STAT_MS at most - a label that is set
// every heartbeat would redraw itself every heartbeat.
void Ui_dispRender(bool ledOn, uint16_t edges) {
    uint32_t const now = (uint32_t)xTaskGetTickCount();
    bool const changed = (!Ui_shownValid_) || (ledOn != Ui_shownOn_)
                         || (edges != Ui_shownEdges_);
    bool const slow = (Ui_statAt_ == 0U)
                      || ((int32_t)(now - Ui_statAt_)
                          >= (int32_t)pdMS_TO_TICKS(UI_STAT_MS));

    if (!Ui_built_) {
        return;   // no widgets yet: the splash still owns the glass
    }

    if (changed) {
        Ui_shownOn_ = ledOn;
        Ui_shownEdges_ = edges;
        Ui_shownValid_ = true;
        W_Topbar_setLed(&Ui_top_, ledOn);
    }

    // One readout both pages share: the load over the window that just
    // passed. The tick hook counts the ticks that found the idle task on the
    // CPU, so everything else is work. At 10 ms a tick and a 500 ms window
    // that is fifty samples - a percentage to the nearest two.
    if (slow) {
        uint32_t idle;
        uint32_t total;

        BSP_cpuSample(&idle, &total);
        uint32_t const dIdle = idle - Ui_cpuIdle_;
        uint32_t const dTotal = total - Ui_cpuTotal_;

        Ui_cpuIdle_ = idle;
        Ui_cpuTotal_ = total;
        if (dTotal > 0U) {
            Ui_cpuPct_ = 100U - ((dIdle * 100U) / dTotal);
        }
        W_Topbar_setCpu(&Ui_top_, (unsigned)Ui_cpuPct_);
        Ui_statAt_ = now;
    }

    if (Ui_page_ == UI_PAGE_DEMO) {
        // The ring follows the AO's edge count, wrapped to one turn, and the
        // number in it spells the same value: a ring nobody can read is
        // decoration, and decoration is what the background art already is.
        if (changed) {
            // Seven steps per edge, and not an lv_anim on purpose: an
            // animated value would re-render the ring on every LVGL tick (10
            // ms) at about 21 ms a frame, which saturates this thread. One
            // update per LED event is 4% of a 500 ms period.
            uint32_t const v = ((uint32_t)edges * 7U) % 101U;

            W_Ring_setValue(&Ui_ring_, (uint16_t)v);
            W_Card_setA(&Ui_cardB_, "%u", (unsigned)edges);
        }
        if (slow) {
            uint32_t const load = WidgetIO_pct(Ui_frameUs_,
                                               UI_FRAME_BUDGET_US);

            W_Card_setBms(&Ui_cardB_, Ui_frameUs_);
            W_Bar_setValue(&Ui_bar_, load);
        }
    }
    else {
        // The stats page is about what is left, so every readout on it is a
        // watermark: the stack's, the trace ring's, the pool's, and the two
        // frame times. The arc and the number inside the ring are the same
        // quantity now - one gauge, one number.
        if (slow) {
            lv_mem_monitor_t mon;
            uint32_t const stackFree =
                (uint32_t)uxTaskGetStackHighWaterMark((TaskHandle_t)0);
            uint32_t const stackPct =
                WidgetIO_pct(UI_STACK_WORDS - stackFree, UI_STACK_WORDS);
            // The tracer's own high-water mark: the fewest free record slots
            // its ring has ever had. It only falls, and 0 means the pump fell
            // behind far enough to drop records. Reading the byte is atomic
            // on this core, and a read one window late costs nothing.
            uint32_t const nMin = (uint32_t)SM_Tracer_inst.nMin;

            lv_mem_monitor(&mon);   // the LVGL pool, in bytes
            W_Ring_setValue(&Ui_ring_, (uint16_t)stackPct);
            W_Card_setA(&Ui_cardA_, "%u w", (unsigned)stackFree);
            W_Card_setB(&Ui_cardA_, "%u", (unsigned)nMin);
            W_Card_setAms(&Ui_cardB_, Ui_frameMin_);
            W_Card_setBms(&Ui_cardB_, Ui_frameMax_);
            W_Bar_setValue(&Ui_bar_, mon.used_pct);
        }
    }
}
//............................................................................
// LVGL hands over one dirty rectangle and the pixels for it. Setting the
// window first makes the panel's address counter walk exactly that rectangle.
static void Ui_flush_(lv_display_t *disp, const lv_area_t *area,
                      uint8_t *px) {
    uint32_t const width = (uint32_t)(area->x2 - area->x1 + 1);
    uint32_t const height = (uint32_t)(area->y2 - area->y1 + 1);

    // The pixels of the rectangle are contiguous from the buffer's start,
    // however narrow the rectangle is: LVGL re-shapes its render buffer to
    // the area it is drawing (the stride is the rectangle's width here, not
    // the display's - a row-by-row push stepping by the display width was
    // tried on 2026-10-12 and made every narrow redraw wrong).
    BSP_lcdSetWindow((uint16_t)area->x1, (uint16_t)area->y1,
                     (uint16_t)area->x2, (uint16_t)area->y2);

    // Arm the channel and wait for it. The barrier publishes the strip's
    // stores before the transfer starts: the DMA reads them as another bus
    // master, not through this core's write buffer.
    __DSB();
    BSP_lcdWritePixelsDma((uint16_t const *)(void const *)px, width * height);
    Ui_dmaWait_(UI_DMA_TIMEOUT_MS);

    // The page's first frame is complete once its last dirty area is on the
    // glass. That is the moment the light can come on - bring-up left the
    // backlight off so the first picture is seen whole instead of seen being
    // drawn - and the moment the pending whole-screen repaint is answered.
    if (Ui_framePending_ && lv_display_flush_is_last(disp)) {
        Ui_framePending_ = false;

        if (!Ui_backlightOn_) {
            Ui_backlightOn_ = true;
            BSP_lcdBacklight(true);
        }
    }

    lv_display_flush_ready(disp);
}
//............................................................................
// Runs in the panel channel's interrupt. It wakes the thread that armed the
// transfer and does nothing else: lv_display_flush_ready() is called by that
// thread microseconds later, so every lv_* call in the firmware stays on the
// UI thread (charter §4.2).
static void Ui_flushDone_(void) {
    BaseType_t woken = pdFALSE;

    (void)vTaskNotifyGiveIndexedFromISR(Ui_waiter_, UI_DMA_NOTIFY, &woken);
    portYIELD_FROM_ISR(woken);
}
//............................................................................
// The flush path's one wait, and the reason it blocks instead of spinning
// (charter §4.8, §7): while it waits the idle task runs, and that is where
// the tracer's pump lives. A timeout means the completion never came, a
// failed transfer means the channel reported an error - both are asserted on
// this thread, where pushing a record and resetting the board is ordinary
// work.
static void Ui_dmaWait_(uint32_t timeoutMs) {
    uint32_t const signalled = ulTaskNotifyTakeIndexed(
        UI_DMA_NOTIFY, pdTRUE, pdMS_TO_TICKS(timeoutMs));

    if ((signalled == 0U) || BSP_lcdDmaFailed()) {
        SM_onAssert("uidma", __LINE__);
    }
}
//............................................................................
// LVGL asks the waiter to release a buffer it still considers in flight. In
// single buffer partial mode that cannot happen - Ui_flush_() returns only
// once its strip has landed - so this is the backstop that keeps LVGL's own
// alternative, a busy wait on disp->flushing, out of the firmware.
static void Ui_flushWait_(lv_display_t *disp) {
    uint32_t spin;

    (void)disp;

    // The defensive path: the flush above already waited for the transfer, so
    // this should never have anything to do. It spins rather than taking the
    // notification, which belongs to the wait above and nothing else.
    for (spin = 0U; BSP_lcdDmaBusy(); ++spin) {
        if (spin > UI_DMA_SPIN_LIMIT) {
            SM_onAssert("uidma", __LINE__);
        }
    }
}
//............................................................................
// LVGL's clock is the FreeRTOS tick, so lv_tick_get() and xTaskGetTickCount()
// agree and nothing needs lv_tick_inc() (charter §3).
static uint32_t Ui_tick_(void) {
    return (uint32_t)xTaskGetTickCount();
}
//............................................................................
// Microseconds, for the frame readout on both pages: the HAL time base is
// TIM1 at 1 MHz (one count per microsecond, wrapping every millisecond), so
// the tick count carries the milliseconds and TIM1->CNT carries the rest.
static uint32_t Ui_us_(void) {
    return ((uint32_t)HAL_GetTick() * 1000U) + (uint32_t)TIM1->CNT;
}
//............................................................................
// Page 1, the demo: the AO's own numbers - the ring follows its edge count,
// and the card carries the edge total and this frame's cost. The beat and the
// LED state are the corner's business now (the topbar). Absolute positions
// throughout, so nothing leans on the theme's spacing (the layout is
// docs/diagrams/sketches/pages-two.html).
static void Ui_buildDemo_(void) {
    lv_obj_t * const screen = lv_screen_active();

    W_Topbar_create(&Ui_top_, screen, "sm_blinky");
    W_Ring_create(&Ui_ring_, screen);
    // The ring is the beat made visible, not the raw count: seven steps per
    // LED edge is about 17 degrees, which the eye catches, and a full turn
    // takes some seven seconds. The raw number stays on the card.
    W_Ring_setUnit(&Ui_ring_, "beat");
    W_Card_create(&Ui_cardB_, screen, 124, 204, "edges", "frame");
    W_Bar_create(&Ui_bar_, screen);
    W_Dots_create(&Ui_dots_, screen, 0U);
}
//............................................................................
// Page 2, the stats: the same screen and the same widget kinds, holding the
// numbers that describe the firmware instead of the AO. Every one of them is
// a real readout, so the page is a diagnostics view that happens to look like
// a dashboard.
static void Ui_buildStats_(void) {
    lv_obj_t * const screen = lv_screen_active();

    W_Topbar_create(&Ui_top_, screen, "STATS");
    W_Ring_create(&Ui_ring_, screen);
    // The pool's number gave up its slot to the tracer's watermark: the bar
    // along the bottom is the pool gauge, so the text was the duplicate.
    W_Ring_setUnit(&Ui_ring_, "%");
    W_Card_create(&Ui_cardA_, screen, 12, 204, "stack free", "tracer nmin");
    W_Card_create(&Ui_cardB_, screen, 124, 204, "frame min", "frame max");
    W_Bar_create(&Ui_bar_, screen);
    W_Dots_create(&Ui_dots_, screen, 1U);
}
