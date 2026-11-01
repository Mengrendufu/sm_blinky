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

    lv_obj_remove_style_all(sw);
    lv_obj_set_style_radius(sw, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(sw, 8, LV_PART_INDICATOR);
    lv_obj_set_style_radius(sw, 6, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sw, -2, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sw, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER,
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_size(sw, 34, 16);
    lv_obj_set_pos(sw, (lv_coord_t)x, (lv_coord_t)y);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x334155U), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x004EF6U),
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

void W_Switch_destroy(struct W_Switch *me) {
    lv_obj_delete((lv_obj_t *)me->sw);
    W_Switch_init(me);
}
