//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "w_bar.h"

#include "lvgl.h"

//............................................................................
void W_Bar_create(struct W_Bar *w, struct _lv_obj_t *parent) {
    lv_obj_t *bar = lv_bar_create((lv_obj_t *)parent);

    lv_obj_set_size(bar, 96, 14);
    lv_obj_set_pos(bar, 16, 302);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_radius(bar, 6, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_shadow_width(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x1E2A32U), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xC6FF3DU),
                              LV_PART_INDICATOR);
    w->bar = (struct _lv_obj_t *)bar;
}
//............................................................................
void W_Bar_setValue(struct W_Bar *w, unsigned pct) {
    lv_bar_set_value((lv_obj_t *)w->bar, (int32_t)pct, LV_ANIM_ON);
}
