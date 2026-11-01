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
    g->min = (struct _lv_obj_t *)0;
    g->max = (struct _lv_obj_t *)0;
    g->percent = NULL;
    g->bytes = NULL;
    g->byteUnit = NULL;
}
//............................................................................
void W_Gauge_create(struct W_Gauge *g, struct _lv_obj_t *parent) {
    lv_obj_t *arc = lv_arc_create((lv_obj_t *)parent);

    // Figma: 98 px ellipse, inside stroke, 210..330 degree arc.
    lv_obj_remove_style_all(arc);
    lv_obj_set_size(arc, 98, 98);
    lv_obj_set_pos(arc, 71, 237);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_bg_angles(arc, 210, 330);
    lv_arc_set_range(arc, 0, 100);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0xFFFFFFU), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0xBEE086U),
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_remove_style(arc, (lv_style_t *)0, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(arc, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    g->arc = (struct _lv_obj_t *)arc;

    g->num = WidgetIO_label(parent, "0", 0xFFFFFFU);
    lv_obj_set_pos(g->num, 98, 255);
    lv_obj_set_size(g->num, 27, 16);
    lv_label_set_long_mode(g->num, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(g->num, LV_TEXT_ALIGN_RIGHT, 0);
    g->percent = WidgetIO_label(parent, "%", 0xFFFFFFU);
    lv_obj_set_pos(g->percent, 125, 255);
    lv_obj_set_size(g->percent, 16, 16);
    lv_obj_set_style_text_align(g->percent, LV_TEXT_ALIGN_CENTER, 0);
    g->unit = WidgetIO_label(parent, "Small", 0xFFFFFFU);
    lv_obj_set_pos(g->unit, 14, 210);
    lv_obj_set_size(g->unit, 41, 17);
    g->min = WidgetIO_label(parent, "E", 0xFFFFFFU);
    lv_obj_set_pos(g->min, 72, 265);
    lv_obj_set_size(g->min, 16, 16);
    lv_obj_set_style_text_align(g->min, LV_TEXT_ALIGN_CENTER, 0);
    g->max = WidgetIO_label(parent, "-", 0xFFFFFFU);
    lv_obj_set_pos(g->max, 143, 265);
    lv_obj_set_size(g->max, 28, 16);
    lv_label_set_long_mode(g->max, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(g->max, LV_TEXT_ALIGN_CENTER, 0);
    g->bytes = WidgetIO_label(parent, "-", 0x6FE97BU);
    lv_obj_set_pos(g->bytes, 184, 209);
    lv_obj_set_size(g->bytes, 29, 17);
    lv_obj_set_style_text_align(g->bytes, LV_TEXT_ALIGN_RIGHT, 0);
    g->byteUnit = WidgetIO_label(parent, "B", 0xFFFFFFU);
    lv_obj_set_pos(g->byteUnit, 215, 209);
    lv_obj_set_size(g->byteUnit, 16, 18);
}

void W_Gauge_setBlockBytes(struct W_Gauge *g, unsigned bytes) {
    lv_label_set_text_fmt(g->bytes, "%u", bytes);
}

void W_Gauge_setMaximum(struct W_Gauge *g, unsigned maximum) {
    lv_label_set_text_fmt((lv_obj_t *)g->max, "%u", maximum);
}
//............................................................................
// Each LVGL setter invalidates its own changed region.
void W_Gauge_setValue(struct W_Gauge *g, unsigned pct) {
    lv_arc_set_value((lv_obj_t *)g->arc, (int32_t)pct);
    lv_label_set_text_fmt((lv_obj_t *)g->num, "%u", pct);
}

void W_Gauge_destroy(struct W_Gauge *me) {
    lv_obj_delete((lv_obj_t *)me->arc);
    lv_obj_delete((lv_obj_t *)me->num);
    lv_obj_delete((lv_obj_t *)me->unit);
    lv_obj_delete((lv_obj_t *)me->min);
    lv_obj_delete((lv_obj_t *)me->max);
    lv_obj_delete(me->percent);
    lv_obj_delete(me->bytes);
    lv_obj_delete(me->byteUnit);
    W_Gauge_init(me);
}
