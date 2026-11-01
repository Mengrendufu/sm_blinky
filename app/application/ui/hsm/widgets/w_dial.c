//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software; it comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
//============================================================================
#include "w_dial.h"

#include "lvgl.h"
#include "widget_io_priv.h"

//............................................................................
void W_Dial_init(struct W_Dial *d) {
    d->arc = (struct _lv_obj_t *)0;
    d->heading = (struct _lv_obj_t *)0;
    d->value = 0U;
}
//............................................................................
void W_Dial_create(struct W_Dial *d, struct _lv_obj_t *parent,
                   int32_t x, int32_t y, int32_t size) {
    lv_obj_t *arc = lv_arc_create((lv_obj_t *)parent);

    lv_obj_remove_style_all(arc);
    lv_obj_set_size(arc, (lv_coord_t)size, (lv_coord_t)size);
    lv_obj_set_pos(arc, (lv_coord_t)x, (lv_coord_t)y);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_bg_angles(arc, 180, 90);
    lv_arc_set_range(arc, 0, 100);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0xFFFFFFU), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x9FF082U),
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_remove_style(arc, (lv_style_t *)0, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(arc, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    d->arc = (struct _lv_obj_t *)arc;
    d->heading = WidgetIO_label(parent, "LIVE ACTIVITY", 0xFFFFFFU);
    lv_obj_set_pos((lv_obj_t *)d->heading, 20, 68);
    W_Dial_setValue(d, 0U);
}
//............................................................................
// LVGL invalidates the arc portions affected by a value change.
void W_Dial_setValue(struct W_Dial *d, unsigned pct) {
    d->value = pct;
    lv_arc_set_value((lv_obj_t *)d->arc, (int32_t)pct);
}

void W_Dial_destroy(struct W_Dial *me) {
    lv_obj_delete((lv_obj_t *)me->arc);
    lv_obj_delete((lv_obj_t *)me->heading);
    W_Dial_init(me);
}
