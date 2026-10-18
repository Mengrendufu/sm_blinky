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

// How often the panel's inversion bit is re-sent. The clobber has no
// schedule, so this one never stops - it is one command, and it is the only
// panel register the run-time path touches (see BSP_lcdReassert).
#define UI_INVON_MS       100U

static lv_display_t *Ui_disp_;
// Everything a page builds lives under this one container, so a page change
// can take the last page away without touching the chrome above it. The
// state machine builds into it through Ui_dispPageRoot().
static lv_obj_t *Ui_pageRoot_;
static lv_image_dsc_t Ui_bgDsc_;      // the background of the page that is up
static lv_image_dsc_t Ui_splashDsc_;  // boot image, slot 2
// One heartbeat's cost, measured, and the extremes it has shown: the stats
// page reads them, so they are a readout and not a probe.
static uint32_t Ui_frameUs_;
static uint32_t Ui_frameMin_;
static uint32_t Ui_frameMax_;
// Only the last strip counts: one completed refresh, even with many areas.
static uint32_t Ui_frames_;
static uint32_t Ui_fpsAt_;
static uint32_t Ui_fps_;

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

// == DEBUG: whether the boot image holds the glass; read over J-Link. ==
volatile uint8_t Ui_dbgSplashOk_;
//============================================================================
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
    Ui_fpsAt_ = lv_tick_get();

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

    // The page container: one parent for every page's widgets, and the one
    // thing a page change empties. It sits below the chrome, which the shared
    // state builds once on the screen itself.
    Ui_pageRoot_ = lv_obj_create(lv_screen_active());
    lv_obj_set_size(Ui_pageRoot_, UI_LCD_HOR, UI_LCD_VER);
    lv_obj_set_pos(Ui_pageRoot_, 0, 0);
    lv_obj_set_style_border_width(Ui_pageRoot_, 0, 0);
    lv_obj_set_style_radius(Ui_pageRoot_, 0, 0);
    lv_obj_set_style_pad_all(Ui_pageRoot_, 0, 0);
    lv_obj_set_style_shadow_width(Ui_pageRoot_, 0, 0);
    lv_obj_remove_flag(Ui_pageRoot_, LV_OBJ_FLAG_SCROLLABLE);
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

//............................................................................
// The parents the state machine draws into: the page root, which a page
// change empties, and the screen above it, where the chrome stays put for the
// whole run.
struct _lv_obj_t *Ui_dispRoot(void) {
    return (struct _lv_obj_t *)lv_screen_active();
}
//............................................................................
struct _lv_obj_t *Ui_dispPageRoot(void) {
    return (struct _lv_obj_t *)Ui_pageRoot_;
}
//............................................................................
void Ui_dispPageEnd(void) {
    lv_obj_clean(Ui_pageRoot_);
}
//............................................................................
// One page entry, whichever page it is: the last page's tree goes, then the
// background that page belongs to is laid down. The page's own state builds
// its widgets on top of it.
void Ui_dispPageBegin(uint32_t slot) {
    Asset const * const asset = Asset_get();
    void const * const bgPixels = Asset_slot(asset, slot);

    Ui_dispPageEnd();
    Ui_screenBase_(Ui_pageRoot_);

    // The bootloader's asset, if it verified: the whole page becomes one
    // image
    // and the widgets sit on top of it. Without it the page keeps the black
    // background, which is the degradation the design counts on.
    if (bgPixels != (void const *)0) {
        lv_obj_t *bg;

        Ui_slotDesc_(&Ui_bgDsc_, bgPixels);

        bg = lv_image_create(Ui_pageRoot_);
        lv_image_set_src(bg, &Ui_bgDsc_);
        lv_obj_align(bg, LV_ALIGN_CENTER, 0, 0);
        lv_obj_remove_flag(bg, LV_OBJ_FLAG_CLICKABLE);
    }

    Ui_framePending_ = true;   // this page starts with a whole screen
}
//............................................................................
// The frame readouts the stats page carries: what the last heartbeat cost,
// and
// the extremes it has shown.
uint32_t Ui_dispFps(void) {
    return Ui_fps_;
}
uint32_t Ui_dispFrameUs(void) {
    return Ui_frameUs_;
}
uint32_t Ui_dispFrameMin(void) {
    return Ui_frameMin_;
}
uint32_t Ui_dispFrameMax(void) {
    return Ui_frameMax_;
}
//............................................................................
bool Ui_dispSplash(void) {
    Asset const * const asset = Asset_get();
    void const * const pixels = Asset_slot(asset, 2U);
    lv_obj_t *img;

    Ui_dbgSplashOk_ = 0U;

    // The boot image is an ordinary widget now, not a push straight to the
    // panel: one writer on the glass, and the state machine owns it. The
    // frame's coverage proof still runs, in the heartbeat, on the first frame
    // this page draws.
    Ui_dispPageEnd();
    Ui_screenBase_(Ui_pageRoot_);

    if (pixels == (void const *)0) {
        return false;   // no asset: the demo page comes up straight away
    }

    Ui_slotDesc_(&Ui_splashDsc_, pixels);

    img = lv_image_create(Ui_pageRoot_);
    lv_image_set_src(img, &Ui_splashDsc_);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);

    Ui_framePending_ = true;   // this page starts with a whole screen
    Ui_dbgSplashOk_ = 1U;

    return true;
}
//............................................................................

//............................................................................
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

    uint32_t const fpsNow = lv_tick_get();
    uint32_t const elapsed = fpsNow - Ui_fpsAt_;

    if (elapsed >= 1000U) {
        Ui_fps_ = (Ui_frames_ * 1000U + elapsed / 2U) / elapsed;
        Ui_frames_ = 0U;
        Ui_fpsAt_ = fpsNow;
    }

    if ((next == LV_NO_TIMER_READY) || (next > UI_FALLBACK_MS)) {
        return pdMS_TO_TICKS(UI_FALLBACK_MS);
    }

    return pdMS_TO_TICKS((next == 0U) ? 1U : next);
}
//............................................................................
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

    if (lv_display_flush_is_last(disp)) {
        ++Ui_frames_;
    }

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
//............................................................................
