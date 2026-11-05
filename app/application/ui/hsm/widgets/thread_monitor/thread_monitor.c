//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "thread_monitor.h"
#include "widgets/helpers/ui_flex.h"
#include "sm_assert.h"

SM_DEFINE_MODULE("thread_monitor")

void UiThreadMonitor_create(UiThreadMonitor *me, lv_obj_t *parent) {
    SM_REQUIRE(me != NULL);
    SM_REQUIRE(parent != NULL);
    SM_REQUIRE(me->container == NULL);
    SM_REQUIRE(me->viewport == NULL);
    SM_REQUIRE(me->cards[0].container == NULL);
    SM_REQUIRE(me->cards[1].container == NULL);
    SM_REQUIRE(me->dots.container == NULL);

    me->container = FlexContainer_create(parent,
        &(UiFlexConfig){
            .width = 236, .height = 105,
            .flow = LV_FLEX_FLOW_COLUMN,
            .mainPlace = LV_FLEX_ALIGN_START,
            .crossPlace = LV_FLEX_ALIGN_CENTER,
            .trackPlace = LV_FLEX_ALIGN_START,
            .padTop = 6, .padLeft = 2, .padRight = 2,
            .padRow = 10});
    lv_obj_remove_flag(me->container, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    me->viewport = lv_obj_create(me->container);
    lv_obj_remove_style_all(me->viewport);
    lv_obj_set_size(me->viewport, 232, 83);
    lv_obj_remove_flag(me->viewport, LV_OBJ_FLAG_SCROLLABLE
                                     | LV_OBJ_FLAG_CLICKABLE
                                     | LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_point_t const cardPositions[2] = {
        {.x = 0, .y = 0},
        {.x = 232, .y = 0}
    };
    for(unsigned i = 0; i < 2; ++i) {
        UiThreadCard_create(&me->cards[i], me->viewport);
        lv_obj_set_pos(me->cards[i].container,
                        cardPositions[i].x, cardPositions[i].y);
    }
    UiDots_create(&me->dots, me->container, 3);
}

void UiThreadMonitor_destroy(UiThreadMonitor *me) {
    SM_REQUIRE(me != NULL);
    for(unsigned i = 0; i < 2; ++i) {
        UiThreadCard_destroy(&me->cards[i]);
    }
    UiDots_destroy(&me->dots);
    if(me->container != NULL) {
        lv_obj_delete(me->container);
    }
    *me = (UiThreadMonitor){0};
}
