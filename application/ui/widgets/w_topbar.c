//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "w_topbar.h"

#include "lvgl.h"
#include "widget_io_priv.h"

//............................................................................
void W_Topbar_init(struct W_Topbar *bar) {
    bar->title = (struct _lv_obj_t *)0;
    bar->fps = (struct _lv_obj_t *)0;
    bar->cpu = (struct _lv_obj_t *)0;
    bar->led = (struct _lv_obj_t *)0;
}
//............................................................................
void W_Topbar_create(struct W_Topbar *bar, struct _lv_obj_t *parent,
                     char const *title) {
    lv_obj_t *led = lv_led_create((lv_obj_t *)parent);

    bar->title = WidgetIO_label(parent, title, 0xE0E4E8U);
    lv_obj_align((lv_obj_t *)bar->title, LV_ALIGN_TOP_LEFT, 8, 6);

    lv_obj_set_size(led, 10, 10);
    lv_obj_align(led, LV_ALIGN_TOP_RIGHT, -8, 7);
    lv_led_set_color(led, lv_color_hex(0x00C853U));
    lv_led_on(led);
    bar->led = (struct _lv_obj_t *)led;

    // Shared readouts, from left to right: FPS, CPU load, heartbeat led.
    bar->fps = WidgetIO_label(parent, "fps --", 0x9AA3ACU);
    lv_obj_align((lv_obj_t *)bar->fps, LV_ALIGN_TOP_RIGHT, -96, 6);
    bar->cpu = WidgetIO_label(parent, "cpu --%", 0x9AA3ACU);
    lv_obj_align((lv_obj_t *)bar->cpu, LV_ALIGN_TOP_RIGHT, -24, 6);
}
//............................................................................
void W_Topbar_setTitle(struct W_Topbar *bar, char const *title) {
    lv_label_set_text((lv_obj_t *)bar->title, title);
}
//............................................................................
void W_Topbar_setFps(struct W_Topbar *bar, unsigned fps) {
    lv_label_set_text_fmt((lv_obj_t *)bar->fps, "fps %u", fps);
}
//............................................................................
void W_Topbar_setCpu(struct W_Topbar *bar, unsigned pct) {
    lv_label_set_text_fmt((lv_obj_t *)bar->cpu, "cpu %u%%", pct);
}
//............................................................................
// The blink is the AO's, not an animation: on and off follow the LED events,
// so what the corner shows and what the board's LED does are the same event.
void W_Topbar_setLed(struct W_Topbar *bar, bool on) {
    lv_obj_t *led = (lv_obj_t *)bar->led;

    lv_obj_set_style_bg_color(led, lv_color_hex(on ? 0x00C853U : 0x303840U),
                              0);
    if (on) {
        lv_led_on(led);
    }
    else {
        lv_led_off(led);
    }
}
