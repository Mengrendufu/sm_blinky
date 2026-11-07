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
                                           lv_style_t const *style) {
    lv_obj_t * const box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_add_style(box, style, LV_PART_MAIN);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return box;
}

static lv_obj_t *UiResourceCard_label_(lv_obj_t *parent,
                                       lv_style_t const *style,
                                       char const *text) {
    return Label_create(parent,
        &(LabelConfig){
            .style = style,
            .longMode = LV_LABEL_LONG_MODE_CLIP,
            .text = text});
}

void UiResourceCard_create(UiResourceCard *me, lv_obj_t *parent,
                            int32_t width, int32_t height) {
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
            .width = width, .height = height,
            .flow = LV_FLEX_FLOW_COLUMN,
            .mainPlace = LV_FLEX_ALIGN_START,
            .crossPlace = LV_FLEX_ALIGN_CENTER,
            .trackPlace = LV_FLEX_ALIGN_START,
            .padTop = 1, .padBottom = 1, .padRow = 4});
    lv_obj_remove_flag(me->container, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    me->header = UiResourceCard_container_(me->container,
                                           &UiStyle_resourceHeader);
    me->name = UiResourceCard_label_(me->header, &UiStyle_resourceName, "--");
    me->sizeGroup = UiResourceCard_container_(me->header,
                                              &UiStyle_resourceSizeGroup);
    me->size = UiResourceCard_label_(me->sizeGroup,
                                     &UiStyle_resourceSize, "--");
    me->unit = UiResourceCard_label_(me->sizeGroup,
                                     &UiStyle_resourceUnit, "B");

    me->gauge = UiResourceCard_container_(me->container,
                                          &UiStyle_resourceGauge);
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
    me->percent = UiResourceCard_label_(me->gauge,
                                        &UiStyle_resourcePercent, "--%");

    me->range = UiResourceCard_container_(me->container,
                                          &UiStyle_resourceRange);
    me->minimum = UiResourceCard_label_(me->range,
                                        &UiStyle_resourceMinimum, "E");
    me->maximum = UiResourceCard_label_(me->range,
                                        &UiStyle_resourceMaximum, "--");
}

void UiResourceCard_destroy(UiResourceCard *me) {
    SM_REQUIRE(me != NULL);
    if(me->container != NULL) {
        lv_obj_delete(me->container);
    }
    *me = (UiResourceCard){0};
}

void UiResourceCard_setHeader(UiResourceCard *me, char const *name,
                              char const *size, char const *unit) {
    SM_REQUIRE(me != NULL && me->container != NULL);
    SM_REQUIRE(name != NULL && size != NULL && unit != NULL);
    lv_label_set_text_static(me->name, name);
    lv_label_set_text_static(me->size, size);
    lv_label_set_text_static(me->unit, unit);
}

void UiResourceCard_setGauge(UiResourceCard *me, char const *percentText,
                             char const *value, char const *total,
                             int32_t percent) {
    SM_REQUIRE(me != NULL && me->container != NULL);
    SM_REQUIRE(percentText != NULL && value != NULL && total != NULL);
    SM_REQUIRE(percent >= 0 && percent <= 100);
    lv_label_set_text_static(me->percent, percentText);
    lv_label_set_text_static(me->minimum, value);
    lv_label_set_text_static(me->maximum, total);
    lv_arc_set_value(me->arc, percent);
}
