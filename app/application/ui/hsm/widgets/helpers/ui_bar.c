//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "ui_bar.h"
#include "sm_assert.h"

SM_DEFINE_MODULE("ui_bar")

lv_obj_t *Bar_create(lv_obj_t *parent, BarConfig const *config) {
    SM_REQUIRE(parent != NULL);
    SM_REQUIRE(config != NULL);
    SM_REQUIRE(config->minValue < config->maxValue);
    SM_REQUIRE(config->value >= config->minValue);
    SM_REQUIRE(config->value <= config->maxValue);

    lv_obj_t * const bar = lv_bar_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, config->width, config->height);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_bar_set_range(bar, config->minValue, config->maxValue);
    lv_bar_set_value(bar, config->value, LV_ANIM_OFF);
    return bar;
}
