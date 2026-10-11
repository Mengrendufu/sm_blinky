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

// The boot splash (main.c) is a whole frame pushed straight to the panel, and
// it is worth looking at: LVGL is held off for this long so it stays up. The
// inbox keeps being drained meanwhile, so no event is lost - they just land
// together in the first real frame.
#define UI_SPLASH_MS 1500U

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

// Which background slot the demo shows. 0 = bg-a (夜色仪表),
// 1 = bg-b (网格科技),
// 2 = bg-c (暮色准照片) - the three the burner wrote, in that order.
#define UI_BG_SLOT 0U

// Temporary measurement: what one refresh really costs, and how much of it is
// our own flush. Two records per refresh that flushed something, so the log
// stays readable:
//   ==TIME==[FRAME][<us total>][<flushes>][<KiB flushed>]==  RecID 39
//   ==TIME==[FLUSH][<us in flush>][<flushes>]==             RecID 40
//   ==TIME==[BLK][<us render><us flush><w/8><h/8>]==        RecID 41
// The difference between 39 and 40 is what LVGL's rendering costs; 41 splits
// that by chunk, and says which chunk: the render time of a chunk is the gap
// between the end of the previous flush and the start of this one.
#define UI_TRACE_FRAME 39U
#define UI_TRACE_FLUSH 40U
#define UI_TRACE_BLK   41U

static lv_display_t *Ui_disp_;
static lv_image_dsc_t Ui_bgDsc_;
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

static uint32_t Ui_flushes_;
static uint32_t Ui_flushPx_;
static uint32_t Ui_flushUs_;
static uint32_t Ui_lastFlushEnd_;
//============================================================================
static void Ui_buildWidgets_(void);
static void Ui_flush_(lv_display_t *disp, const lv_area_t *area,
                      uint8_t *px);
static void Ui_flushDone_(void);
static void Ui_flushWait_(lv_display_t *disp);
static void Ui_dmaWait_(void);
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

    Ui_buildWidgets_();
}
//............................................................................
TickType_t Ui_dispHeartbeat(void) {
    static TickType_t Ui_splashUntil_;
    uint32_t const before = Ui_us_();

    // Hold LVGL off while the boot splash is up. The deadline starts on the
    // first call, which is the loop's first pass - right after the widgets
    // were built, so the splash has the screen to itself.
    if (Ui_splashUntil_ == 0U) {
        Ui_splashUntil_ = xTaskGetTickCount() + pdMS_TO_TICKS(UI_SPLASH_MS);
    }
    if ((int32_t)(xTaskGetTickCount() - Ui_splashUntil_) < 0) {
        return pdMS_TO_TICKS(UI_SPLASH_MS);
    }

    uint32_t const next = lv_timer_handler();
    uint32_t const took = Ui_us_() - before;

    if (Ui_flushes_ > 0U) {
        uint32_t const flushes = Ui_flushes_;
        uint32_t const kib = Ui_flushPx_ / 1024U;

        Ui_flushes_ = 0U;
        Ui_flushPx_ = 0U;

        SM_TRACE_BEGIN(UI_TRACE_FRAME, 6)
            SM_TRACE_PAYLOAD_BE((took >> 24) & 0xFFU)
            SM_TRACE_PAYLOAD_BE((took >> 16) & 0xFFU)
            SM_TRACE_PAYLOAD_BE((took >> 8) & 0xFFU)
            SM_TRACE_PAYLOAD_BE(took & 0xFFU)
            SM_TRACE_PAYLOAD_BE((uint8_t)((flushes > 255U) ? 255U : flushes))
            SM_TRACE_PAYLOAD_BE((uint8_t)((kib > 255U) ? 255U : kib))
        SM_TRACE_END()

        SM_TRACE_BEGIN(UI_TRACE_FLUSH, 5)
            SM_TRACE_PAYLOAD_BE((Ui_flushUs_ >> 24) & 0xFFU)
            SM_TRACE_PAYLOAD_BE((Ui_flushUs_ >> 16) & 0xFFU)
            SM_TRACE_PAYLOAD_BE((Ui_flushUs_ >> 8) & 0xFFU)
            SM_TRACE_PAYLOAD_BE(Ui_flushUs_ & 0xFFU)
            SM_TRACE_PAYLOAD_BE((uint8_t)((flushes > 255U) ? 255U : flushes))
        SM_TRACE_END()

        Ui_flushUs_ = 0U;
        Ui_lastFlushEnd_ = 0U;   // the next refresh starts a new chain
    }

    if ((next == LV_NO_TIMER_READY) || (next > UI_FALLBACK_MS)) {
        return pdMS_TO_TICKS(UI_FALLBACK_MS);
    }

    return pdMS_TO_TICKS((next == 0U) ? 1U : next);
}
//............................................................................
void Ui_dispRender(bool ledOn, uint16_t edges) {
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
    uint32_t const entered = Ui_us_();
    uint32_t const width = (uint32_t)(area->x2 - area->x1 + 1);
    uint32_t const height = (uint32_t)(area->y2 - area->y1 + 1);
    uint32_t const gap = (Ui_lastFlushEnd_ == 0U)
                         ? 0U : (entered - Ui_lastFlushEnd_);

    BSP_lcdSetWindow((uint16_t)area->x1, (uint16_t)area->y1,
                     (uint16_t)area->x2, (uint16_t)area->y2);

    // Arm the channel and wait for it. The barrier publishes the strip's
    // stores before the transfer starts: the DMA reads them as another bus
    // master, not through this core's write buffer.
    __DSB();
    BSP_lcdWritePixelsDma((uint16_t const *)(void const *)px, width * height);
    Ui_dmaWait_();

    ++Ui_flushes_;
    Ui_flushPx_ += width * height;

    {
        uint32_t const left = Ui_us_();
        uint32_t const inFlush = left - entered;
        uint32_t const gapClamped = (gap > 65535U) ? 65535U : gap;
        uint32_t const flushClamped = (inFlush > 65535U) ? 65535U : inFlush;

        Ui_flushUs_ += inFlush;
        Ui_lastFlushEnd_ = left;

        SM_TRACE_BEGIN(UI_TRACE_BLK, 6)
            SM_TRACE_PAYLOAD_BE((gapClamped >> 8) & 0xFFU)
            SM_TRACE_PAYLOAD_BE(gapClamped & 0xFFU)
            SM_TRACE_PAYLOAD_BE((flushClamped >> 8) & 0xFFU)
            SM_TRACE_PAYLOAD_BE(flushClamped & 0xFFU)
            SM_TRACE_PAYLOAD_BE((uint8_t)(width / 8U))
            SM_TRACE_PAYLOAD_BE((uint8_t)(height / 8U))
        SM_TRACE_END()
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
static void Ui_dmaWait_(void) {
    uint32_t const signalled = ulTaskNotifyTakeIndexed(
        UI_DMA_NOTIFY, pdTRUE, pdMS_TO_TICKS(UI_DMA_TIMEOUT_MS));

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
// Microseconds, for the measurement above: the HAL time base is TIM1 at 1 MHz
// (one count per microsecond, wrapping every millisecond), so the tick count
// carries the milliseconds and TIM1->CNT the rest.
static uint32_t Ui_us_(void) {
    return ((uint32_t)HAL_GetTick() * 1000U) + (uint32_t)TIM1->CNT;
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

    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    // Black, the same black BSP_lcdInit() leaves in the frame memory: the
    // screen has a real background (moving or removing a widget must repaint
    // what it covered), but it matches the panel's own idle colour, so the
    // first refresh does not tint the whole glass on the way in.
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000U), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    // The bootloader's asset, if it verified: the whole screen becomes one
    // image and the widgets sit on top of it. The descriptor points
    // straight at the external SRAM - nothing is copied, nothing is decoded.
    // Without it the screen keeps the black background, which is the
    // degradation the design counts on.
    if (bgPixels != (void const *)0) {
        lv_obj_t *bg;

        Ui_bgDsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
        Ui_bgDsc_.header.cf = LV_COLOR_FORMAT_RGB565;
        Ui_bgDsc_.header.w = UI_LCD_HOR;
        Ui_bgDsc_.header.h = UI_LCD_VER;
        Ui_bgDsc_.header.stride = UI_LCD_HOR * 2U;
        Ui_bgDsc_.data_size = ASSET_SLOT_BYTES;
        Ui_bgDsc_.data = (uint8_t const *)bgPixels;

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
