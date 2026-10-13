//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "w_switch.h"

#include "lvgl.h"

//............................................................................
void W_Switch_init(struct W_Switch *w) {
    w->sw = (struct _lv_obj_t *)0;
}
//............................................................................
void W_Switch_create(struct W_Switch *w, struct _lv_obj_t *parent,
                     int32_t x, int32_t y) {
    lv_obj_t *sw = lv_switch_create((lv_obj_t *)parent);

    lv_obj_set_size(sw, 56, 28);
    lv_obj_set_pos(sw, (lv_coord_t)x, (lv_coord_t)y);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x1E2A32U), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x00C3BCU),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0xFFFFFFU), LV_PART_KNOB);
    w->sw = (struct _lv_obj_t *)sw;
}
//............................................................................
void W_Switch_set(struct W_Switch *w, bool on) {
    if (on) {
        lv_obj_add_state((lv_obj_t *)w->sw, LV_STATE_CHECKED);
    }
    else {
        lv_obj_remove_state((lv_obj_t *)w->sw, LV_STATE_CHECKED);
    }
}
