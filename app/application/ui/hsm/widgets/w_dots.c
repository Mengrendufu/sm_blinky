//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "w_dots.h"

#include "lvgl.h"

//............................................................................
void W_Dots_create(struct W_Dots *w, struct _lv_obj_t *parent,
                   unsigned active) {
    unsigned i;

    for (i = 0U; i < 6U; ++i) {
        lv_obj_t *dot = lv_obj_create((lv_obj_t *)parent);

        lv_obj_set_size(dot, 6, 6);
        lv_obj_set_pos(dot, (lv_coord_t)(105 + (int32_t)(i % 3U) * 12),
                       (lv_coord_t)(i < 3U ? 176 : 304));
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_shadow_width(dot, 0, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot,
                                  lv_color_hex((i % 3U == 0U) ? 0x6FE97BU
                                                             : 0x334155U), 0);
        if (active != 0U) {
            lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        }
        w->dot[i] = (struct _lv_obj_t *)dot;
    }
}

void W_Dots_init(struct W_Dots *me) {
    for (unsigned i = 0U; i < 6U; ++i) {
        me->dot[i] = NULL;
    }
}

void W_Dots_destroy(struct W_Dots *me) {
    for (unsigned i = 0U; i < 6U; ++i) {
        lv_obj_delete((lv_obj_t *)me->dot[i]);
    }
    W_Dots_init(me);
}
