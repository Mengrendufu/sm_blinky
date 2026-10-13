//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "w_pulse.h"

#include "lvgl.h"

//............................................................................
static void W_Pulse_anim_(void * obj, int32_t v) {
    lv_bar_set_value((lv_obj_t *)obj, v, LV_ANIM_OFF);
}
//............................................................................
void W_Pulse_init(struct W_Pulse *w) {
    w->bar = (struct _lv_obj_t *)0;
}
//............................................................................
void W_Pulse_create(struct W_Pulse *w, struct _lv_obj_t *parent,
                    int32_t x, int32_t y, int32_t width) {
    lv_obj_t *bar = lv_bar_create((lv_obj_t *)parent);
    lv_anim_t a;

    lv_obj_set_size(bar, (lv_coord_t)width, 12);
    lv_obj_set_pos(bar, (lv_coord_t)x, (lv_coord_t)y);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar, 6, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_shadow_width(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x1E2A32U), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x00C3BCU),
                              LV_PART_INDICATOR);
    w->bar = (struct _lv_obj_t *)bar;

    lv_anim_init(&a);
    lv_anim_set_var(&a, bar);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_duration(&a, 1200);
    lv_anim_set_playback_duration(&a, 1200);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, W_Pulse_anim_);
    lv_anim_start(&a);
}
