//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "thread_card.h"
#include "widgets/helpers/ui_bar.h"
#include "widgets/helpers/ui_flex.h"
#include "widgets/helpers/ui_label.h"
#include "widgets/styles/ui_style.h"
#include "sm_assert.h"

SM_DEFINE_MODULE("thread_card")

static void UiThreadCard_createResource_(UiThreadCardResource *row,
                                         lv_obj_t *parent) {
    row->container = lv_obj_create(parent);
    lv_obj_remove_style_all(row->container);
    lv_obj_set_size(row->container, 232, 23);
    lv_obj_remove_flag(row->container, LV_OBJ_FLAG_SCROLLABLE
                                       | LV_OBJ_FLAG_CLICKABLE
                                       | LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    row->value = Label_create(row->container,
        &(LabelConfig){
            .width = 174, .height = 17,
            .longMode = LV_LABEL_LONG_MODE_CLIP,
            .textAlign = LV_TEXT_ALIGN_LEFT,
            .text = ""});
    lv_obj_add_style(row->value, &UiStyle_threadCardText, LV_PART_MAIN);
    lv_obj_align(row->value, LV_ALIGN_TOP_LEFT, 0, 0);
    row->unit = Label_create(row->container,
        &(LabelConfig){
            .width = 58, .height = 17,
            .longMode = LV_LABEL_LONG_MODE_CLIP,
            .textAlign = LV_TEXT_ALIGN_RIGHT,
            .text = ""});
    lv_obj_add_style(row->unit, &UiStyle_threadCardText, LV_PART_MAIN);
    lv_obj_align(row->unit, LV_ALIGN_TOP_RIGHT, 0, 0);
    row->bar = Bar_create(row->container,
        &(BarConfig){
            .width = 232, .height = 6,
            .minValue = 0, .maxValue = 100, .value = 0});
    lv_obj_add_style(row->bar, &UiStyle_barTrack, LV_PART_MAIN);
    lv_obj_add_style(row->bar, &UiStyle_barIndicator, LV_PART_INDICATOR);
    lv_obj_align(row->bar, LV_ALIGN_BOTTOM_MID, 0, 0);
}

void UiThreadCard_create(UiThreadCard *me, lv_obj_t *parent) {
    SM_REQUIRE(me != NULL);
    SM_REQUIRE(parent != NULL);
    SM_REQUIRE(me->container == NULL);
    SM_REQUIRE(me->header == NULL);
    SM_REQUIRE(me->threadName == NULL);
    SM_REQUIRE(me->priority == NULL);
    for(unsigned i = 0; i < UI_THREAD_CARD_RESOURCE_COUNT; ++i) {
        SM_REQUIRE(me->resources[i].container == NULL);
        SM_REQUIRE(me->resources[i].value == NULL);
        SM_REQUIRE(me->resources[i].unit == NULL);
        SM_REQUIRE(me->resources[i].bar == NULL);
    }

    lv_obj_t * const box = FlexContainer_create(parent,
        &(UiFlexConfig){
            .width = 232, .height = 83,
            .flow = LV_FLEX_FLOW_COLUMN,
            .mainPlace = LV_FLEX_ALIGN_START,
            .crossPlace = LV_FLEX_ALIGN_START,
            .trackPlace = LV_FLEX_ALIGN_START,
            .padRow = 10});
    lv_obj_remove_flag(box, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    me->container = box;

    me->header = lv_obj_create(box);
    lv_obj_remove_style_all(me->header);
    lv_obj_set_size(me->header, 232, 17);
    lv_obj_remove_flag(me->header, LV_OBJ_FLAG_SCROLLABLE
                                   | LV_OBJ_FLAG_CLICKABLE
                                   | LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    me->threadName = Label_create(me->header,
        &(LabelConfig){
            .width = 174, .height = 17,
            .longMode = LV_LABEL_LONG_MODE_CLIP,
            .textAlign = LV_TEXT_ALIGN_LEFT,
            .text = ""});
    lv_obj_add_style(me->threadName, &UiStyle_threadCardText, LV_PART_MAIN);
    lv_obj_align(me->threadName, LV_ALIGN_LEFT_MID, 0, 0);
    me->priority = Label_create(me->header,
        &(LabelConfig){
            .width = 58, .height = 17,
            .longMode = LV_LABEL_LONG_MODE_CLIP,
            .textAlign = LV_TEXT_ALIGN_RIGHT,
            .text = ""});
    lv_obj_add_style(me->priority, &UiStyle_threadCardText, LV_PART_MAIN);
    lv_obj_align(me->priority, LV_ALIGN_RIGHT_MID, 0, 0);
    for(unsigned i = 0; i < UI_THREAD_CARD_RESOURCE_COUNT; ++i) {
        UiThreadCard_createResource_(&me->resources[i], box);
    }
}

void UiThreadCard_destroy(UiThreadCard *me) {
    SM_REQUIRE(me != NULL);
    if(me->container != NULL) {
        lv_obj_delete(me->container);
    }
    *me = (UiThreadCard){0};
}

void UiThreadCard_setHeader(UiThreadCard *me, char const *threadName,
                            char const *priorityText) {
    SM_REQUIRE(me != NULL);
    SM_REQUIRE(me->container != NULL);
    SM_REQUIRE(me->header != NULL);
    SM_REQUIRE(me->threadName != NULL);
    SM_REQUIRE(me->priority != NULL);
    SM_REQUIRE(threadName != NULL);
    SM_REQUIRE(priorityText != NULL);
    lv_label_set_text_static(me->threadName, threadName);
    lv_label_set_text_static(me->priority, priorityText);
}

void UiThreadCard_setResource(UiThreadCard *me, UiThreadCardResourceId id,
                              char const *value, char const *unit,
                              int32_t percent) {
    SM_REQUIRE(me != NULL);
    SM_REQUIRE(me->container != NULL);
    SM_REQUIRE(id == UI_THREAD_CARD_STACK || id == UI_THREAD_CARD_INBOX);
    SM_REQUIRE(value != NULL);
    SM_REQUIRE(unit != NULL);
    SM_REQUIRE(percent >= 0 && percent <= 100);
    UiThreadCardResource * const row = &me->resources[id];
    SM_REQUIRE(row->container != NULL);
    SM_REQUIRE(row->value != NULL);
    SM_REQUIRE(row->unit != NULL);
    SM_REQUIRE(row->bar != NULL);
    lv_label_set_text_static(row->value, value);
    lv_label_set_text_static(row->unit, unit);
    lv_bar_set_value(row->bar, percent, LV_ANIM_OFF);
}
