//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "live_motion.h"
#include "widgets/helpers/ui_flex.h"
#include "widgets/helpers/ui_label.h"
#include "widgets/helpers/ui_bar.h"
#include "widgets/styles/ui_style.h"
#include "sm_assert.h"

SM_DEFINE_MODULE("live_motion")

static lv_obj_t *UiLiveMotion_label_(lv_obj_t *parent, char const *text) {
    return Label_create(parent,
        &(LabelConfig){
            .style = &UiStyle_liveLabel,
            .longMode = LV_LABEL_LONG_MODE_CLIP, .text = text});
}

void UiLiveMotion_create(UiLiveMotion *me, lv_obj_t *parent,
                          int32_t width, int32_t height) {
    SM_REQUIRE(me != NULL);
    SM_REQUIRE(parent != NULL);
    SM_REQUIRE(me->container == NULL);
    SM_REQUIRE(me->arc.container == NULL);
    me->container = FlexContainer_create(parent,
        &(UiFlexConfig){
            .width = width, .height = height, .flow = LV_FLEX_FLOW_COLUMN,
            .mainPlace = LV_FLEX_ALIGN_START,
            .crossPlace = LV_FLEX_ALIGN_START,
            .trackPlace = LV_FLEX_ALIGN_START,
            .padTop = 15, .padLeft = 10, .padRight = 10, .padRow = 7});
    me->header = FlexContainer_create(me->container,
        &(UiFlexConfig){
            .width = lv_pct(100), .height = 18, .flow = LV_FLEX_FLOW_ROW,
            .mainPlace = LV_FLEX_ALIGN_SPACE_BETWEEN,
            .crossPlace = LV_FLEX_ALIGN_CENTER,
            .trackPlace = LV_FLEX_ALIGN_START, .padColumn = 8});
    me->heading = UiLiveMotion_label_(me->header, "LIVE ACTIVITY");
    lv_obj_set_width(me->heading, LV_SIZE_CONTENT);
    me->ledStatus = lv_switch_create(me->header);
    lv_obj_remove_style_all(me->ledStatus);
    lv_obj_remove_flag(me->ledStatus,
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(me->ledStatus, &UiStyle_switchTrack, LV_PART_MAIN);
    lv_obj_add_style(me->ledStatus, &UiStyle_switchIndicator,
                      LV_PART_INDICATOR);
    lv_obj_add_style(me->ledStatus, &UiStyle_switchChecked,
                      LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_style(me->ledStatus, &UiStyle_switchKnob, LV_PART_KNOB);
    me->body = FlexContainer_create(me->container,
        &(UiFlexConfig){
            .width = lv_pct(100), .height = 70, .flow = LV_FLEX_FLOW_ROW,
            .mainPlace = LV_FLEX_ALIGN_SPACE_BETWEEN,
            .crossPlace = LV_FLEX_ALIGN_CENTER,
            .trackPlace = LV_FLEX_ALIGN_START, .padColumn = 12});
    UiArc_create(&me->arc, me->body, 70, 70);
    me->details = FlexContainer_create(me->body,
        &(UiFlexConfig){
            .width = 100, .height = lv_pct(100),
            .flow = LV_FLEX_FLOW_COLUMN,
            .mainPlace = LV_FLEX_ALIGN_SPACE_BETWEEN,
            .crossPlace = LV_FLEX_ALIGN_START,
            .trackPlace = LV_FLEX_ALIGN_START});
    lv_obj_set_flex_grow(me->details, 1);
    me->caption = UiLiveMotion_label_(me->details, "INDICATOR RUN");
    me->value = UiLiveMotion_label_(me->details, "");
    me->bar = Bar_create(me->details,
        &(BarConfig){
            .width = lv_pct(100), .height = 6,
            .minValue = 0, .maxValue = 100, .value = 0});
    lv_obj_add_style(me->bar, &UiStyle_barTrack, LV_PART_MAIN);
    lv_obj_add_style(me->bar, &UiStyle_barIndicator, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(me->bar, lv_color_hex(0x6FE97BU),
                              LV_PART_INDICATOR);
    UiLiveMotion_setValue(me, 0);
}

void UiLiveMotion_setValue(UiLiveMotion *me, uint8_t value) {
    SM_REQUIRE(me != NULL);
    SM_REQUIRE(me->container != NULL);
    SM_REQUIRE(value <= 100U);
    UiArc_setValue(&me->arc, value);
    (void)lv_snprintf(me->text, sizeof me->text, "%u%%", (unsigned)value);
    lv_label_set_text_static(me->value, me->text);
    lv_bar_set_value(me->bar, value, LV_ANIM_OFF);
}

void UiLiveMotion_setLedStatus(UiLiveMotion *me, bool on) {
    SM_REQUIRE(me != NULL && me->ledStatus != NULL);
    if (on) {
        lv_obj_add_state(me->ledStatus, LV_STATE_CHECKED);
    }
    else {
        lv_obj_remove_state(me->ledStatus, LV_STATE_CHECKED);
    }
}

void UiLiveMotion_setProgress(UiLiveMotion *me, uint16_t progress) {
    SM_REQUIRE(me != NULL && me->container != NULL);
    SM_REQUIRE(progress < 1000U);
    // Smoothstep on a triangular ramp, with per-mille precision.
    uint32_t const ramp = progress <= 500U
                          ? progress * 2U : (1000U - progress) * 2U;
    uint8_t const value = (uint8_t)(
        (ramp * ramp * (3000U - 2U * ramp) + 5000000U) / 10000000U);
    me->progress = progress;
    UiLiveMotion_setValue(me, value);
    UiLiveMotion_setLedStatus(me, progress < 500U);
}

void UiLiveMotion_destroy(UiLiveMotion *me) {
    SM_REQUIRE(me != NULL);
    UiArc_destroy(&me->arc);
    if (me->container != NULL) {
        lv_obj_delete(me->container);
    }
    *me = (UiLiveMotion){0};
}
