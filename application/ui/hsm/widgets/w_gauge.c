//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "w_gauge.h"

#include "lvgl.h"
#include "widget_io_priv.h"

//............................................................................
void W_Gauge_init(struct W_Gauge *g) {
    g->arc = (struct _lv_obj_t *)0;
    g->num = (struct _lv_obj_t *)0;
    g->unit = (struct _lv_obj_t *)0;
}
//............................................................................
void W_Gauge_create(struct W_Gauge *g, struct _lv_obj_t *parent) {
    lv_obj_t *arc = lv_arc_create((lv_obj_t *)parent);

    // The widget's box is a circle even though only its top half is painted,
    // so the draw cost follows the 156x156 box. W_Gauge_setValue() narrows
    // the
    // dirty band to the painted half, which is where the cost is cut back.
    lv_obj_set_size(arc, 156, 156);
    lv_obj_set_pos(arc, 42, 28);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_bg_angles(arc, 180, 0);
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
    g->arc = (struct _lv_obj_t *)arc;

    g->num = WidgetIO_label(parent, "0 %", 0xFFFFFFU);
    lv_obj_align((lv_obj_t *)g->num, LV_ALIGN_TOP_MID, 0, 88);

    g->unit = WidgetIO_label(parent, "cpu", 0x9AA3ACU);
    lv_obj_align((lv_obj_t *)g->unit, LV_ALIGN_TOP_MID, 0, 104);
}
//............................................................................
// Value and number move together, and the dirty area is the arc's painted
// half
// across the full width - never a narrow column, which LVGL 9.5 draws with
// wrong pixels (see w_ring.c for the measurement). Three 240x32 strips.
void W_Gauge_setValue(struct W_Gauge *g, unsigned pct) {
    lv_area_t box;
    lv_area_t band;

    lv_arc_set_value((lv_obj_t *)g->arc, (int32_t)pct);
    lv_label_set_text_fmt((lv_obj_t *)g->num, "%u %%", pct);

    lv_obj_get_coords((lv_obj_t *)g->arc, &box);
    band.x1 = 0;
    band.x2 = (lv_coord_t)(lv_display_get_horizontal_resolution(
                               lv_display_get_default()) - 1);
    band.y1 = box.y1;
    band.y2 = (lv_coord_t)((box.y1 + box.y2) / 2);
    lv_obj_invalidate_area(lv_screen_active(), &band);
}

void W_Gauge_destroy(struct W_Gauge *me) {
    lv_obj_delete((lv_obj_t *)me->arc);
    lv_obj_delete((lv_obj_t *)me->num);
    lv_obj_delete((lv_obj_t *)me->unit);
    W_Gauge_init(me);
}
