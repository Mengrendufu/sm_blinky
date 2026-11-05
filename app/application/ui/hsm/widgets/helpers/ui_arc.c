//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "ui_arc.h"
#include "sm_assert.h"

SM_DEFINE_MODULE("ui_arc")

lv_obj_t *Arc_create(lv_obj_t *parent, ArcConfig const *config) {
    SM_REQUIRE(parent != NULL);
    SM_REQUIRE(config != NULL);
    SM_REQUIRE(config->startAngle <= 360);
    SM_REQUIRE(config->endAngle <= 360);
    SM_REQUIRE(config->minValue < config->maxValue);
    SM_REQUIRE(config->value >= config->minValue);
    SM_REQUIRE(config->value <= config->maxValue);

    lv_obj_t * const arc = lv_arc_create(parent);
    lv_obj_remove_style_all(arc);
    lv_obj_set_size(arc, config->width, config->height);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_bg_angles(arc, config->startAngle, config->endAngle);
    lv_arc_set_range(arc, config->minValue, config->maxValue);
    lv_arc_set_value(arc, config->value);
    return arc;
}
