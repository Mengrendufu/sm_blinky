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
    bar->fpsUnit = NULL;
    bar->cpuUnit = NULL;
    bar->led = (struct _lv_obj_t *)0;
    bar->count = (struct _lv_obj_t *)0;
    bar->cpuValid = false;
    bar->fpsValid = false;
    bar->countValid = false;
}
//............................................................................
void W_Topbar_create(struct W_Topbar *bar, struct _lv_obj_t *parent,
                     char const *title) {
    lv_obj_t *led = lv_obj_create((lv_obj_t *)parent);
    lv_obj_remove_style_all(led);
    lv_obj_set_style_radius(led, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(led, LV_OPA_COVER, 0);

    bar->title = WidgetIO_label(parent, title, 0xFFFFFFU);
    lv_obj_align((lv_obj_t *)bar->title, LV_ALIGN_TOP_LEFT, 2, 31);

    lv_obj_set_size(led, 10, 10);
    lv_obj_align(led, LV_ALIGN_TOP_LEFT, 2, 7);
    lv_obj_set_style_bg_color(led, lv_color_hex(0xD9D9D9U), 0);
    bar->led = (struct _lv_obj_t *)led;

    bar->count = WidgetIO_label(parent, "-", 0xFFFFFFU);
    lv_obj_set_pos((lv_obj_t *)bar->count, 13, 2);
    lv_obj_set_size(bar->count, 90, 20);
    lv_obj_set_style_pad_top(bar->count, 2, 0);
    lv_obj_set_style_text_align((lv_obj_t *)bar->count,
                                LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode((lv_obj_t *)bar->count, LV_LABEL_LONG_MODE_CLIP);

    bar->fps = WidgetIO_label(parent, "--", 0xFFFFFFU);
    lv_obj_set_pos(bar->fps, 142, 4);
    lv_obj_set_size(bar->fps, 27, 17);
    lv_label_set_long_mode(bar->fps, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(bar->fps, LV_TEXT_ALIGN_RIGHT, 0);
    bar->fpsUnit = WidgetIO_label(parent, "FPS", 0xFFFFFFU);
    lv_obj_set_pos(bar->fpsUnit, 170, 4);
    lv_obj_set_size(bar->fpsUnit, 28, 17);
    bar->cpu = WidgetIO_label(parent, "--", 0xFFFFFFU);
    lv_obj_set_pos(bar->cpu, 199, 4);
    lv_obj_set_size(bar->cpu, 27, 17);
    lv_label_set_long_mode(bar->cpu, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(bar->cpu, LV_TEXT_ALIGN_RIGHT, 0);
    bar->cpuUnit = WidgetIO_label(parent, "%", 0xFFFFFFU);
    lv_obj_set_pos(bar->cpuUnit, 227, 4);
    lv_obj_set_size(bar->cpuUnit, 13, 17);
    lv_label_set_long_mode(bar->cpuUnit, LV_LABEL_LONG_MODE_CLIP);
}
//............................................................................
void W_Topbar_setTitle(struct W_Topbar *bar, char const *title) {
    lv_label_set_text((lv_obj_t *)bar->title, title);
}
//............................................................................
void W_Topbar_setFps(struct W_Topbar *bar, unsigned fps) {
    if (bar->fpsValid && bar->shownFps == fps) {
        return;
    }
    bar->shownFps = fps;
    bar->fpsValid = true;
    lv_label_set_text_fmt((lv_obj_t *)bar->fps, "%u", fps);
}
//............................................................................
void W_Topbar_setCpu(struct W_Topbar *bar, unsigned pct) {
    if (bar->cpuValid && bar->shownCpu == pct) {
        return;
    }
    bar->shownCpu = pct;
    bar->cpuValid = true;
    lv_label_set_text_fmt((lv_obj_t *)bar->cpu, "%u", pct);
}
//............................................................................
void W_Topbar_setCount(struct W_Topbar *bar, uint32_t count) {
    if (bar->countValid && bar->shownCount == count) {
        return;
    }
    bar->shownCount = count;
    bar->countValid = true;
    lv_label_set_text_fmt((lv_obj_t *)bar->count, "%u", (unsigned)count);
}
//............................................................................
// The blink is the AO's, not an animation: on and off follow the LED events,
// so what the corner shows and what the board's LED does are the same event.
void W_Topbar_setLed(struct W_Topbar *bar, bool on) {
    lv_obj_t *led = (lv_obj_t *)bar->led;

    lv_obj_set_style_bg_color(led,
        lv_color_hex(on ? 0xD9D9D9U : 0x475569U), 0);
}

void W_Topbar_destroy(struct W_Topbar *bar) {
    lv_obj_delete((lv_obj_t *)bar->title);
    lv_obj_delete((lv_obj_t *)bar->fps);
    lv_obj_delete((lv_obj_t *)bar->cpu);
    lv_obj_delete((lv_obj_t *)bar->led);
    lv_obj_delete((lv_obj_t *)bar->count);
    lv_obj_delete(bar->fpsUnit);
    lv_obj_delete(bar->cpuUnit);
    W_Topbar_init(bar);
}
