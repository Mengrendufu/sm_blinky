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

#include "bsp.h"
#include "bsp_lcd.h"
#include "sm_tracer.h"
#include "ui_disp_priv.h"
#include "sm_assert.h"
#include "lvgl.h"
#include "FreeRTOS.h"
#include "task.h"

SM_DEFINE_MODULE("ui_disp")

//============================================================================
#define UI_LCD_HOR 240
#define UI_LCD_VER 320

// 240 x 32 pixels x 2 B = 15,360 B, the single partial buffer of charter §6.
#define UI_STRIP_ROWS 32U
#define UI_STRIP_PX   (UI_LCD_HOR * UI_STRIP_ROWS)

// The longest the UI thread may sleep without checking LVGL. Used when LVGL
// has no timer pending, and as the ceiling for what it does ask for.
#define UI_FALLBACK_MS 10U

// The notification slot the flush waits on. Slot 0 wakes the loop for the
// inbox (ui.c); slot 1 is the "the blocking call you submitted has finished"
// slot from AGENTS.md, shared with the flash RPC - this thread blocks on one
// thing at a time, so one slot serves both.
#define UI_DMA_NOTIFY 1U

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
static BSP_Lcd *Ui_lcd_;
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

// The HSM requests a whole-screen repaint. Clear the request only after
// its last synchronous flush; backlight policy belongs to the HSM.
static bool Ui_framePending_;

//============================================================================
static void Ui_flush_(lv_display_t *disp, const lv_area_t *area,
                      uint8_t *px);
static void Ui_lcdBindWait_(void);
static void Ui_flushWait_(lv_display_t *disp);
static uint32_t Ui_tick_(void);
static uint32_t Ui_us_(void);

//============================================================================
void Ui_dispInit(void) {
    lv_init();
    lv_tick_set_cb(Ui_tick_);
    Ui_fpsAt_ = lv_tick_get();

    Ui_disp_ = lv_display_create(UI_LCD_HOR, UI_LCD_VER);
    SM_ENSURE(Ui_disp_ != (lv_display_t *)0);

    // Supply scheduling capabilities; the BSP owns the transfer wait flow.
    Ui_lcdBindWait_();
    lv_display_set_flush_wait_cb(Ui_disp_, Ui_flushWait_);

    lv_display_set_flush_cb(Ui_disp_, Ui_flush_);
    lv_display_set_buffers(Ui_disp_, Ui_strip_, (void *)0, sizeof(Ui_strip_),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

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
// Track a requested whole-screen refresh through its final synchronous
// flush. The HSM owns page objects and backlight timing.
void Ui_dispRequestFrame(void) {
    Ui_framePending_ = true;
}
//............................................................................
// The HSM chooses when the panel becomes visible; BSP enforces the owner.
void Ui_dispBacklight(bool on) {
    BSP_lcdBacklight(Ui_lcd_, on);
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
        BSP_lcdReassert(Ui_lcd_);
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
    // The pixels of the rectangle are contiguous from the buffer's start,
    // however narrow the rectangle is: LVGL re-shapes its render buffer to
    // the area it is drawing (the stride is the rectangle's width here, not
    // the display's - a row-by-row push stepping by the display width was
    // tried on 2026-10-12 and made every narrow redraw wrong).
    if (!BSP_lcdWriteArea(Ui_lcd_, (uint16_t)area->x1, (uint16_t)area->y1,
                              (uint16_t)area->x2, (uint16_t)area->y2,
                              (uint16_t const *)(void const *)px)) {
        SM_onAssert("uidma", __LINE__);
    }

    if (lv_display_flush_is_last(disp)) {
        ++Ui_frames_;
        Ui_framePending_ = false;
    }

    lv_display_flush_ready(disp);
}
//............................................................................
// LCD wait capabilities. The BSP sees only an opaque context and hooks;
// task identity, notification slot and ISR scheduling stay in this adapter.
static bool Ui_lcdIsCaller_(void *context) {
    return (context != (void *)0)
           && (xPortIsInsideInterrupt() == pdFALSE)
           && (xTaskGetCurrentTaskHandle() == (TaskHandle_t)context);
}

static bool Ui_lcdWaitDone_(void *context, uint32_t timeoutMs) {
    (void)context;
    return ulTaskNotifyTakeIndexed(UI_DMA_NOTIFY, pdTRUE,
                                   pdMS_TO_TICKS(timeoutMs)) != 0U;
}

static void Ui_lcdNotifyDone_(void *context) {
    TaskHandle_t const waiter = (TaskHandle_t)context;

    if (xPortIsInsideInterrupt() != pdFALSE) {
        BaseType_t woken = pdFALSE;

        (void)vTaskNotifyGiveIndexedFromISR(waiter, UI_DMA_NOTIFY, &woken);
        portYIELD_FROM_ISR(woken);
    }
    else {
        (void)xTaskNotifyGiveIndexed(waiter, UI_DMA_NOTIFY);
    }
}

static void Ui_lcdYield_(void *context) {
    (void)context;
    taskYIELD();
}

static void Ui_lcdBindWait_(void) {
    static BSP_lcdWaitOperations const operations = {
        .isCaller = &Ui_lcdIsCaller_,
        .waitDone = &Ui_lcdWaitDone_,
        .notifyDone = &Ui_lcdNotifyDone_,
        .yield = &Ui_lcdYield_,
    };

    Ui_lcd_ = BSP_lcdDevice();
    BSP_lcdBind(Ui_lcd_, &BSP_lcdDmaOperations, &operations,
                 (void *)xTaskGetCurrentTaskHandle());
}
//............................................................................
// Flush completes synchronously and calls flush_ready before returning.
// LVGL only invokes this hook when flushing is still set: that would break
// the single-buffer contract. Fail-stop rather than spin or consume a second
// notification. lv_* completion stays on the UI thread, never in the ISR.
static void Ui_flushWait_(lv_display_t *disp) {
    (void)disp;
    SM_onAssert("uiflush", __LINE__);
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
