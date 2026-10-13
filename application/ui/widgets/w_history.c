//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "w_history.h"

#include "lvgl.h"
#include "widget_io_priv.h"

//............................................................................
void W_History_init(struct W_History *w) {
    unsigned i;

    w->cap = (struct _lv_obj_t *)0;
    for (i = 0U; i < W_HISTORY_N; ++i) {
        w->bar[i] = (struct _lv_obj_t *)0;
        w->fill[i] = 0U;
    }
}
//............................................................................
void W_History_create(struct W_History *w, struct _lv_obj_t *parent) {
    unsigned i;

    for (i = 0U; i < W_HISTORY_N; ++i) {
        lv_obj_t *bar = lv_bar_create((lv_obj_t *)parent);

        lv_obj_set_size(bar, 12, 40);
        lv_obj_set_pos(bar, (lv_coord_t)(30 + (int32_t)i * 15), 224);
        lv_bar_set_range(bar, 0, 100);
        lv_obj_set_style_radius(bar, 2, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_shadow_width(bar, 0, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0xC6FF3DU),
                                  LV_PART_INDICATOR);
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        w->bar[i] = (struct _lv_obj_t *)bar;
    }

    w->cap = WidgetIO_label(parent, "cpu history: 6 s", 0x9AA3ACU);
    lv_obj_align((lv_obj_t *)w->cap, LV_ALIGN_TOP_MID, 0, 270);
}
//............................................................................
// The bars are drawn from the bottom up, so a sample is a height, and the
// shift is one value write per bar - no animation, no history buffer.
void W_History_push(struct W_History *w, unsigned pct) {
    unsigned i;

    for (i = 0U; (i + 1U) < W_HISTORY_N; ++i) {
        w->fill[i] = w->fill[i + 1U];
    }
    w->fill[W_HISTORY_N - 1U] = (uint8_t)pct;

    for (i = 0U; i < W_HISTORY_N; ++i) {
        lv_bar_set_value((lv_obj_t *)w->bar[i], (int32_t)w->fill[i],
                         LV_ANIM_OFF);
    }
}
