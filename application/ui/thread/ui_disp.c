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
#include "ui_disp_priv.h"
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

// Which background slot the demo shows. 0 = bg-a (夜色仪表),
// 1 = bg-b (网格科技),
// 2 = bg-c (暮色准照片) - the three the burner wrote, in that order.
#define UI_BG_SLOT 0U

static lv_display_t *Ui_disp_;
static lv_image_dsc_t Ui_bgDsc_;      // demo page background, slot 0
static lv_image_dsc_t Ui_splashDsc_;  // boot image, slot 2
static lv_obj_t *Ui_led_;
static lv_obj_t *Ui_state_;
static lv_obj_t *Ui_edges_;

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
static void Ui_buildWidgets_(void);
static void Ui_flush_(lv_display_t *disp, const lv_area_t *area,
                      uint8_t *px);
static void Ui_flushDone_(void);
static void Ui_flushWait_(lv_display_t *disp);
static void Ui_dmaWait_(uint32_t timeoutMs);
static uint32_t Ui_tick_(void);

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
// The demo page starts. The widgets were built at bring-up and the model has
// moved on since - the AO keeps beating while the boot image is up - so the
// next render has to write it all out again instead of trusting what the
// widgets already show.
void Ui_dispDemo(void) {
    lv_obj_t * const screen = lv_screen_active();

    // Every page builds its own tree, from a clean screen: the splash's image
    // is gone before the demo's exists, so the two can never share the glass.
    lv_obj_clean(screen);
    Ui_built_ = false;

    Ui_buildWidgets_();

    // This page too starts with a whole screen: ask for one, and let the
    // model be written out again on the next render.
    Ui_framePending_ = true;
    Ui_shownValid_ = false;
}
//............................................................................
TickType_t Ui_dispHeartbeat(void) {
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

    if ((next == LV_NO_TIMER_READY) || (next > UI_FALLBACK_MS)) {
        return pdMS_TO_TICKS(UI_FALLBACK_MS);
    }

    return pdMS_TO_TICKS((next == 0U) ? 1U : next);
}
//............................................................................
void Ui_dispRender(bool ledOn, uint16_t edges) {
    if (!Ui_built_) {
        return;   // no widgets yet: the splash still owns the glass
    }

    if (Ui_shownValid_ && (ledOn == Ui_shownOn_)
        && (edges == Ui_shownEdges_)) {
        return;
    }

    Ui_shownOn_ = ledOn;
    Ui_shownEdges_ = edges;
    Ui_shownValid_ = true;

    lv_obj_set_style_bg_color(Ui_led_,
                              lv_color_hex(ledOn ? 0x00C853U : 0x303840U), 0);
    lv_label_set_text(Ui_state_, ledOn ? "LED ON" : "LED OFF");
    lv_label_set_text_fmt(Ui_edges_, "edges %u", (unsigned)edges);
}
//............................................................................
// LVGL hands over one dirty rectangle and the pixels for it. Setting the
// window first makes the panel's address counter walk exactly that rectangle.
static void Ui_flush_(lv_display_t *disp, const lv_area_t *area,
                      uint8_t *px) {
    uint32_t const width = (uint32_t)(area->x2 - area->x1 + 1);
    uint32_t const height = (uint32_t)(area->y2 - area->y1 + 1);

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
// The demo screen: a title, one square that follows the Blinky AO's LED, and
// the edge count the HSM keeps. Absolute positions, no layout engine, so
// nothing here depends on the theme's spacing.
static void Ui_buildWidgets_(void) {
    lv_obj_t * const screen = lv_screen_active();
    lv_obj_t *title;
    Asset const * const asset = Asset_get();
    void const * const bgPixels = Asset_slot(asset, UI_BG_SLOT);

    if (Ui_built_) {
        return;   // a re-entry to the demo page must not duplicate the tree
    }
    Ui_built_ = true;

    Ui_screenBase_(screen);

    // The bootloader's asset, if it verified: the whole screen becomes one
    // image and the widgets sit on top of it. Without it the screen keeps the
    // black background, which is the degradation the design counts on.
    if (bgPixels != (void const *)0) {
        lv_obj_t *bg;

        Ui_slotDesc_(&Ui_bgDsc_, bgPixels);

        bg = lv_image_create(screen);
        lv_image_set_src(bg, &Ui_bgDsc_);
        lv_obj_align(bg, LV_ALIGN_CENTER, 0, 0);
        lv_obj_remove_flag(bg, LV_OBJ_FLAG_CLICKABLE);
    }

    title = lv_label_create(screen);
    lv_label_set_text(title, "sm_blinky");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE0E4E8U), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    Ui_led_ = lv_obj_create(screen);
    lv_obj_set_size(Ui_led_, 72, 72);
    lv_obj_set_style_radius(Ui_led_, 12, 0);
    lv_obj_set_style_border_width(Ui_led_, 0, 0);
    lv_obj_align(Ui_led_, LV_ALIGN_CENTER, 0, -48);

    Ui_state_ = lv_label_create(screen);
    lv_obj_set_style_text_color(Ui_state_, lv_color_hex(0xE0E4E8U), 0);
    lv_obj_align(Ui_state_, LV_ALIGN_CENTER, 0, 44);

    Ui_edges_ = lv_label_create(screen);
    lv_obj_set_style_text_color(Ui_edges_, lv_color_hex(0x808890U), 0);
    lv_obj_align(Ui_edges_, LV_ALIGN_CENTER, 0, 72);

}
