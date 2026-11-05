//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "ui_flex.h"
#include "sm_assert.h"

SM_DEFINE_MODULE("ui_flex")

lv_obj_t *FlexContainer_create(lv_obj_t *parent, UiFlexConfig const *config) {
    SM_REQUIRE(parent != NULL);
    SM_REQUIRE(config != NULL);

    lv_obj_t * const obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, config->width, config->height);
    lv_obj_set_flex_flow(obj, config->flow);
    lv_obj_set_flex_align(obj, config->mainPlace, config->crossPlace,
                          config->trackPlace);
    lv_obj_set_style_pad_top(obj, config->padTop, 0);
    lv_obj_set_style_pad_bottom(obj, config->padBottom, 0);
    lv_obj_set_style_pad_left(obj, config->padLeft, 0);
    lv_obj_set_style_pad_right(obj, config->padRight, 0);
    lv_obj_set_style_pad_row(obj, config->padRow, 0);
    lv_obj_set_style_pad_column(obj, config->padColumn, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}
