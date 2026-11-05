//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "dots.h"
#include "widgets/helpers/ui_flex.h"
#include "widgets/styles/ui_style.h"
#include "sm_assert.h"

SM_DEFINE_MODULE("dots")

void UiDots_create(UiDots *me, lv_obj_t *parent, uint8_t count) {
    SM_REQUIRE(me != NULL);
    SM_REQUIRE(parent != NULL);
    SM_REQUIRE(me->container == NULL);
    SM_REQUIRE(count > 0);
    me->container = FlexContainer_create(parent,
        &(UiFlexConfig){
            .width = (int32_t)count * 12 - 6, .height = 6,
            .flow = LV_FLEX_FLOW_ROW,
            .mainPlace = LV_FLEX_ALIGN_START,
            .crossPlace = LV_FLEX_ALIGN_START,
            .trackPlace = LV_FLEX_ALIGN_START,
            .padColumn = 6});
    for(unsigned i = 0; i < count; ++i) {
        lv_obj_t * const dot = lv_obj_create(me->container);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 6, 6);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE
                                | LV_OBJ_FLAG_CLICKABLE
                                | LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        lv_obj_add_style(dot, &UiStyle_dot, LV_PART_MAIN);
        if(i == 0) {
            lv_obj_add_style(dot, &UiStyle_dotActive, LV_PART_MAIN);
        }
    }
}

void UiDots_setActive(UiDots *me, uint8_t index) {
    SM_REQUIRE(me != NULL);
    SM_REQUIRE(me->container != NULL);
    uint32_t const count = lv_obj_get_child_count(me->container);
    SM_REQUIRE(index < count);
    for (uint32_t i = 0; i < count; ++i) {
        lv_obj_t * const dot = lv_obj_get_child(me->container, (int32_t)i);
        lv_obj_remove_style(dot, &UiStyle_dotActive, LV_PART_MAIN);
        if (i == index) {
            lv_obj_add_style(dot, &UiStyle_dotActive, LV_PART_MAIN);
        }
    }
}

void UiDots_destroy(UiDots *me) {
    SM_REQUIRE(me != NULL);
    if(me->container != NULL) {
        lv_obj_delete(me->container);
    }
    *me = (UiDots){0};
}
