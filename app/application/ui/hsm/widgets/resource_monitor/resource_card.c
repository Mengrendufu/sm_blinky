//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "resource_card.h"
#include "widgets/helpers/ui_arc.h"
#include "widgets/helpers/ui_flex.h"
#include "widgets/helpers/ui_label.h"
#include "widgets/styles/ui_style.h"
#include "sm_assert.h"

SM_DEFINE_MODULE("resource_card")

static lv_obj_t *UiResourceCard_container_(lv_obj_t *parent,
                                           int32_t width,
                                           int32_t height) {
    lv_obj_t * const box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, width, height);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return box;
}

static lv_obj_t *UiResourceCard_label_(lv_obj_t *parent, int32_t width,
                                       char const *text,
                                       lv_text_align_t align) {
    lv_obj_t * const label = Label_create(parent,
        &(LabelConfig){
            .width = width, .height = 17,
            .longMode = LV_LABEL_LONG_MODE_CLIP,
            .textAlign = align, .text = text});
    lv_obj_add_style(label, &UiStyle_resourceCardText, LV_PART_MAIN);
    return label;
}

void UiResourceCard_create(UiResourceCard *me, lv_obj_t *parent) {
    SM_REQUIRE(me != NULL);
    SM_REQUIRE(parent != NULL);
    SM_REQUIRE(me->container == NULL);
    SM_REQUIRE(me->header == NULL);
    SM_REQUIRE(me->sizeGroup == NULL);
    SM_REQUIRE(me->name == NULL);
    SM_REQUIRE(me->size == NULL);
    SM_REQUIRE(me->unit == NULL);
    SM_REQUIRE(me->gauge == NULL);
    SM_REQUIRE(me->arc == NULL);
    SM_REQUIRE(me->percent == NULL);
    SM_REQUIRE(me->range == NULL);
    SM_REQUIRE(me->minimum == NULL);
    SM_REQUIRE(me->maximum == NULL);
    me->container = FlexContainer_create(parent,
        &(UiFlexConfig){
            .width = 236, .height = 118,
            .flow = LV_FLEX_FLOW_COLUMN,
            .mainPlace = LV_FLEX_ALIGN_START,
            .crossPlace = LV_FLEX_ALIGN_CENTER,
            .trackPlace = LV_FLEX_ALIGN_START,
            .padTop = 1, .padBottom = 1, .padRow = 4});
    lv_obj_remove_flag(me->container, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    me->header = UiResourceCard_container_(me->container, 236, 27);
    me->name = UiResourceCard_label_(me->header, 174, "--",
                                     LV_TEXT_ALIGN_LEFT);
    lv_obj_align(me->name, LV_ALIGN_TOP_LEFT, 2, 0);
    me->sizeGroup = UiResourceCard_container_(me->header, 27, 17);
    lv_obj_align(me->sizeGroup, LV_ALIGN_TOP_RIGHT, -2, 0);
    me->size = UiResourceCard_label_(me->sizeGroup, 16, "--",
                                     LV_TEXT_ALIGN_RIGHT);
    me->unit = UiResourceCard_label_(me->sizeGroup, 11, "B",
                                     LV_TEXT_ALIGN_RIGHT);
    lv_obj_align(me->unit, LV_ALIGN_TOP_RIGHT, 0, 0);

    me->gauge = UiResourceCard_container_(me->container, 102, 35);
    // Figma uses a full ellipse box for a short upper arc. Only this
    // wrapper permits overflow; the card and viewport still clip.
    lv_obj_add_flag(me->gauge, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    me->arc = Arc_create(me->gauge,
        &(ArcConfig){
            .width = 100, .height = 100,
            .startAngle = 207, .endAngle = 333,
            .minValue = 0, .maxValue = 100, .value = 0});
    lv_obj_set_pos(me->arc, 1, 6);
    lv_obj_add_style(me->arc, &UiStyle_arcTrack, LV_PART_MAIN);
    lv_obj_add_style(me->arc, &UiStyle_arcIndicator,
                      LV_PART_INDICATOR);
    me->percent = UiResourceCard_label_(me->gauge, 36, "--%",
                                        LV_TEXT_ALIGN_CENTER);
    lv_obj_set_pos(me->percent, 33, 21);

    me->range = UiResourceCard_container_(me->container, 236, 17);
    me->minimum = UiResourceCard_label_(me->range, 10, "E",
                                        LV_TEXT_ALIGN_LEFT);
    lv_obj_set_pos(me->minimum, 66, 0);
    me->maximum = UiResourceCard_label_(me->range, 34, "--",
                                        LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_pos(me->maximum, 136, 0);
}

void UiResourceCard_destroy(UiResourceCard *me) {
    SM_REQUIRE(me != NULL);
    if(me->container != NULL) {
        lv_obj_delete(me->container);
    }
    *me = (UiResourceCard){0};
}
