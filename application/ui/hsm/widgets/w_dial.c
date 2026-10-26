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

//............................................................................
void W_Dial_init(struct W_Dial *d) {
    d->arc = (struct _lv_obj_t *)0;
    d->value = 0U;
    d->rising = true;
}
//............................................................................
void W_Dial_create(struct W_Dial *d, struct _lv_obj_t *parent,
                   int32_t x, int32_t y, int32_t size) {
    lv_obj_t *arc = lv_arc_create((lv_obj_t *)parent);

    lv_obj_set_size(arc, (lv_coord_t)size, (lv_coord_t)size);
    lv_obj_set_pos(arc, (lv_coord_t)x, (lv_coord_t)y);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_bg_angles(arc, 210, 90);
    lv_arc_set_range(arc, 0, 100);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x1B2830U), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x00C3BCU),
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
    lv_obj_remove_style(arc, (lv_style_t *)0, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(arc, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    d->arc = (struct _lv_obj_t *)arc;
    d->rising = true;
    W_Dial_setValue(d, 0U);
}
//............................................................................
// One step, one dirty band: the arc's whole rows across the full width. A
// narrow area is what the arc renderer gets wrong (see w_ring.c).
void W_Dial_setValue(struct W_Dial *d, unsigned pct) {
    lv_area_t box;
    lv_area_t band;

    d->value = pct;
    lv_arc_set_value((lv_obj_t *)d->arc, (int32_t)pct);

    lv_obj_get_coords((lv_obj_t *)d->arc, &box);
    band.x1 = 0;
    band.x2 = (lv_coord_t)(lv_display_get_horizontal_resolution(
                               lv_display_get_default()) - 1);
    band.y1 = box.y1;
    band.y2 = box.y2;
    lv_obj_invalidate_area(lv_screen_active(), &band);
}

void W_Dial_step(struct W_Dial *d) {
    unsigned value = d->value;
    if (d->rising) {
        value += 2U;
        if (value >= 100U) {
            value = 100U;
            d->rising = false;
        }
    } else {
        value = (value > 2U) ? value - 2U : 0U;
        if (value == 0U) {
            d->rising = true;
        }
    }
    W_Dial_setValue(d, value);
}
