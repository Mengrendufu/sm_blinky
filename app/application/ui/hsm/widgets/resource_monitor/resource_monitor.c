//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "resource_monitor.h"
#include "sm_assert.h"

SM_DEFINE_MODULE("resource_monitor")

void UiResourceMonitor_create(UiResourceMonitor *me, lv_obj_t *parent) {
    SM_REQUIRE(me != NULL);
    SM_REQUIRE(parent != NULL);
    SM_REQUIRE(me->container == NULL);
    SM_REQUIRE(me->viewport == NULL);
    SM_REQUIRE(me->indicatorContainer == NULL);
    SM_REQUIRE(me->cards[0].container == NULL);
    SM_REQUIRE(me->cards[1].container == NULL);
    SM_REQUIRE(me->dots.container == NULL);
    me->container = lv_obj_create(parent);
    lv_obj_remove_style_all(me->container);
    lv_obj_set_size(me->container, 236, 144);
    lv_obj_remove_flag(me->container, LV_OBJ_FLAG_SCROLLABLE
                                      | LV_OBJ_FLAG_CLICKABLE
                                      | LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    me->viewport = lv_obj_create(me->container);
    lv_obj_remove_style_all(me->viewport);
    lv_obj_set_size(me->viewport, 236, 118);
    lv_obj_set_pos(me->viewport, 0, 26);
    lv_obj_remove_flag(me->viewport, LV_OBJ_FLAG_SCROLLABLE
                                     | LV_OBJ_FLAG_CLICKABLE
                                     | LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_point_t const cardPositions[2] = {
        {.x = 0, .y = 0},
        {.x = 236, .y = 0}
    };
    for(unsigned i = 0; i < 2; ++i) {
        UiResourceCard_create(&me->cards[i], me->viewport);
        lv_obj_set_pos(me->cards[i].container,
                        cardPositions[i].x, cardPositions[i].y);
    }
    // Figma's indicator region overlaps the card's lower range area.
    // Keep it outside the viewport so changing cards cannot move it.
    me->indicatorContainer = lv_obj_create(me->container);
    lv_obj_remove_style_all(me->indicatorContainer);
    lv_obj_set_size(me->indicatorContainer, 236, 46);
    lv_obj_set_pos(me->indicatorContainer, 0, 97);
    lv_obj_remove_flag(me->indicatorContainer, LV_OBJ_FLAG_SCROLLABLE
                           | LV_OBJ_FLAG_CLICKABLE);
    UiDots_create(&me->dots, me->indicatorContainer, 4);
    lv_obj_set_pos(me->dots.container, 97, 16);
}

void UiResourceMonitor_destroy(UiResourceMonitor *me) {
    SM_REQUIRE(me != NULL);
    for(unsigned i = 0; i < 2; ++i) {
        UiResourceCard_destroy(&me->cards[i]);
    }
    UiDots_destroy(&me->dots);
    if(me->container != NULL) {
        lv_obj_delete(me->container);
    }
    *me = (UiResourceMonitor){0};
}
