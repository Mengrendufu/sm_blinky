//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "arc.h"
#include "widgets/helpers/ui_arc.h"
#include "widgets/helpers/ui_label.h"
#include "widgets/styles/ui_style.h"
#include "sm_assert.h"

SM_DEFINE_MODULE("arc")

void UiArc_create(UiArc *me, lv_obj_t *parent,
                   int32_t width, int32_t height) {
    SM_REQUIRE(me != NULL);
    SM_REQUIRE(parent != NULL);
    SM_REQUIRE(me->container == NULL && me->percent == NULL);
    me->container = Arc_create(parent,
        &(ArcConfig){
            .width = width, .height = height,
            .startAngle = 180, .endAngle = 90,
            .minValue = 0, .maxValue = 100, .value = 0});
    lv_obj_add_style(me->container, &UiStyle_arcTrack, LV_PART_MAIN);
    lv_obj_add_style(me->container, &UiStyle_arcIndicator,
                      LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(me->container, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(me->container, 10, LV_PART_INDICATOR);
    me->percent = Label_create(me->container,
        &(LabelConfig){
            .width = lv_pct(100), .height = 17,
            .longMode = LV_LABEL_LONG_MODE_CLIP,
            .textAlign = LV_TEXT_ALIGN_CENTER, .text = ""});
    lv_obj_add_style(me->percent, &UiStyle_liveText, LV_PART_MAIN);
    lv_obj_center(me->percent);
    UiArc_setValue(me, 0);
}

void UiArc_setValue(UiArc *me, uint8_t value) {
    SM_REQUIRE(me != NULL);
    SM_REQUIRE(me->container != NULL && me->percent != NULL);
    SM_REQUIRE(value <= 100U);
    (void)lv_snprintf(me->text, sizeof me->text, "%u%%", (unsigned)value);
    lv_arc_set_value(me->container, value);
    lv_label_set_text_static(me->percent, me->text);
}

void UiArc_destroy(UiArc *me) {
    SM_REQUIRE(me != NULL);
    if (me->container != NULL) {
        lv_obj_delete(me->container);
    }
    *me = (UiArc){0};
}
