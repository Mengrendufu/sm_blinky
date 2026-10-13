//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "w_ring.h"

#include "lvgl.h"
#include "widget_io_priv.h"

//............................................................................
void W_Ring_init(struct W_Ring *ring) {
    ring->arc = (struct _lv_obj_t *)0;
    ring->num = (struct _lv_obj_t *)0;
    ring->unit = (struct _lv_obj_t *)0;
}
//............................................................................
void W_Ring_create(struct W_Ring *ring, struct _lv_obj_t *parent) {
    lv_obj_t *arc = lv_arc_create((lv_obj_t *)parent);

    lv_obj_set_size(arc, 160, 160);
    lv_obj_set_pos(arc, 40, 28);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    // The angles carry the rotation - never lv_arc_set_rotation. The widget
    // combines the two differently in the two places that matter: the draw
    // path adds rotation raw (lv_arc.c:834-849, which for a 240 degree gauge
    // means 210..450), while the invalidation normalises the angle to
    // [0,360) first and only then adds it (lv_arc.c:866-881). Drawn and
    // invalidated then disagree about a wedge of the ring, which is where the
    // stale rectangular patches came from as the value grew.
    lv_arc_set_bg_angles(arc, 210, 90);
    lv_arc_set_range(arc, 0, 100);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x1B2830U), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x00C3BCU),
                               LV_PART_INDICATOR);
    // Flat ends: the rounded cap is drawn as a width x width circle mask that
    // the renderer blends through, and that blend is the next suspect for the
    // small rectangular patch the arc leaves behind as it grows (2026-10-12).
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
    // The knob goes away entirely, styles and all. Making it transparent is
    // not enough: the default theme gives it a shadow, and an invisible knob
    // with a shadow is a blurry blob that the arc leaves behind at every
    // position it passes - which reads as garbage along the ring, growing
    // with the value (measured on the glass 2026-10-12).
    lv_obj_remove_style(arc, (lv_style_t *)0, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(arc, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    ring->arc = (struct _lv_obj_t *)arc;

    // The number sits at the default font for now. Scaling a label was tried
    // and taken back out (2026-10-12): a transformed object makes the
    // refresher take the layer/matrix path (lv_refr.c:520-592), which wants
    // ARGB8888 layers - and this build has LV_DRAW_SW_SUPPORT_ARGB8888 0, so
    // the draw came out as garbage on the glass. A big readout has to come
    // from a font, not from a transform.
    ring->num = WidgetIO_label(parent, "0", 0xFFFFFFU);
    lv_obj_align((lv_obj_t *)ring->num, LV_ALIGN_TOP_MID, 0, 96);

    ring->unit = WidgetIO_label(parent, "edges", 0x9AA3ACU);
    lv_obj_align((lv_obj_t *)ring->unit, LV_ALIGN_TOP_MID, 0, 140);
}
//............................................................................
void W_Ring_setUnit(struct W_Ring *ring, char const *unit) {
    lv_label_set_text((lv_obj_t *)ring->unit, unit);
}
//............................................................................
// Value and number move together, and the dirty area is the arc's whole y
// band across the full width - never just the arc's own columns, which LVGL
// 9.5 draws with wrong pixels (measured on the glass, 2026-10-12, with the
// pushed rectangles identical in the clean and the wrong case). Six 240x32
// strips are what the glass was measured clean with: about 21 ms against the
// whole screen's 34.8.
void W_Ring_setValue(struct W_Ring *ring, uint16_t value) {
    lv_area_t box;
    lv_area_t band;

    lv_arc_set_value((lv_obj_t *)ring->arc, (int32_t)value);
    lv_label_set_text_fmt((lv_obj_t *)ring->num, "%u", (unsigned)value);

    lv_obj_get_coords((lv_obj_t *)ring->arc, &box);
    band.x1 = 0;
    band.x2 = (lv_coord_t)(lv_display_get_horizontal_resolution(
                               lv_display_get_default()) - 1);
    band.y1 = box.y1;
    band.y2 = box.y2;
    lv_obj_invalidate_area(lv_screen_active(), &band);
}
