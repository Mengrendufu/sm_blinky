//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "w_spinner.h"

#include "lvgl.h"

//............................................................................
void W_Spinner_init(struct W_Spinner *w) {
    w->spin = (struct _lv_obj_t *)0;
}
//............................................................................
void W_Spinner_create(struct W_Spinner *w, struct _lv_obj_t *parent,
                      int32_t x, int32_t y, int32_t size) {
    lv_obj_t *spin = lv_spinner_create((lv_obj_t *)parent);

    lv_obj_set_size(spin, (lv_coord_t)size, (lv_coord_t)size);
    lv_obj_set_pos(spin, (lv_coord_t)x, (lv_coord_t)y);
    lv_obj_remove_flag(spin, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(spin, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spin, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spin, lv_color_hex(0x1B2830U), LV_PART_MAIN);
    lv_obj_set_style_arc_color(spin, lv_color_hex(0x00C3BCU),
                               LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(spin, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_spinner_set_anim_params(spin, 1200, 90);
    w->spin = (struct _lv_obj_t *)spin;
}
