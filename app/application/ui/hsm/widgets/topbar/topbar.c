//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "topbar.h"
#include "widgets/helpers/ui_flex.h"
#include "widgets/helpers/ui_label.h"
#include "widgets/styles/ui_style.h"
#include "sm_assert.h"

SM_DEFINE_MODULE("topbar")

#define TOPBAR_LED_OFF 0xD9D9D9U
#define TOPBAR_LED_ON  0x40E080U

void UiTopbar_create(UiTopbar *me, lv_obj_t *parent,
                      int32_t width, int32_t height) {
    SM_REQUIRE(me != NULL);
    SM_REQUIRE(parent != NULL);
    SM_REQUIRE(me->container == NULL);

    // bar container
    lv_obj_t * const bar = FlexContainer_create(parent,
        &(UiFlexConfig){
            .width = width, .height = height, .flow = LV_FLEX_FLOW_ROW,
            .mainPlace = LV_FLEX_ALIGN_SPACE_BETWEEN,
            .crossPlace = LV_FLEX_ALIGN_CENTER,
            .trackPlace = LV_FLEX_ALIGN_START,
            .padRow = 0, .padColumn = 0});
    me->container = bar;

    // [led] [blinkyCounts]
    lv_obj_t * const left = FlexContainer_create(bar,
        &(UiFlexConfig){
            .width = 99, .height = lv_pct(100), .flow = LV_FLEX_FLOW_ROW,
            .mainPlace = LV_FLEX_ALIGN_SPACE_BETWEEN,
            .crossPlace = LV_FLEX_ALIGN_CENTER,
            .trackPlace = LV_FLEX_ALIGN_START,
            .padRow = 0, .padColumn = 0});
    me->led = lv_obj_create(left);
    lv_obj_remove_style_all(me->led);
    lv_obj_set_size(me->led, 10, 10);
    lv_obj_set_style_radius(me->led, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(me->led, LV_OPA_COVER, 0);
    lv_obj_remove_flag(me->led, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(me->led, LV_OBJ_FLAG_CLICKABLE);
    me->blinkyCount = Label_create(left,
        &(LabelConfig){
            // One pixel wider than Figma; preserve its right edge.
            .width = 86, .height = 17,
            .longMode = LV_LABEL_LONG_MODE_CLIP,
            .textAlign = LV_TEXT_ALIGN_RIGHT,
            .text = me->blinkyCountText});
    lv_obj_add_style(me->blinkyCount, &UiStyle_topbarText, LV_PART_MAIN);

    // [fps] [cpu_usage]
    lv_obj_t * const right = FlexContainer_create(bar,
        &(UiFlexConfig){
            // Native "999FPS" needs 55 px, versus Figma's 52 px.
            // Expand leftwards while preserving FPS/CPU right edges.
            .width = 93, .height = lv_pct(100), .flow = LV_FLEX_FLOW_ROW,
            .mainPlace = LV_FLEX_ALIGN_START,
            .crossPlace = LV_FLEX_ALIGN_CENTER,
            .trackPlace = LV_FLEX_ALIGN_START,
            .padRow = 0, .padColumn = 2});
    me->fps = Label_create(right,
        &(LabelConfig){
            .width = 55, .height = 17,
            .longMode = LV_LABEL_LONG_MODE_CLIP,
            .textAlign = LV_TEXT_ALIGN_RIGHT,
            .text = me->fpsText});
    lv_obj_add_style(me->fps, &UiStyle_topbarText, LV_PART_MAIN);
    me->cpu = Label_create(right,
        &(LabelConfig){
            .width = 36, .height = 17,
            .longMode = LV_LABEL_LONG_MODE_CLIP,
            .textAlign = LV_TEXT_ALIGN_RIGHT,
            .text = me->cpuText});
    lv_obj_add_style(me->cpu, &UiStyle_topbarText, LV_PART_MAIN);
    UiTopbar_setLed(me, false);
    UiTopbar_setCount(me, 0U);
    UiTopbar_setFps(me, 0U);
    UiTopbar_setCpu(me, 0U);
}

void UiTopbar_destroy(UiTopbar *me) {
    SM_REQUIRE(me != NULL);
    if (me->container != NULL) {
        lv_obj_delete(me->container);
        *me = (UiTopbar){0};
    }
}

void UiTopbar_setLed(UiTopbar *me, bool on) {
    SM_REQUIRE(me != NULL);
    SM_REQUIRE(me->container != NULL);
    lv_obj_set_style_bg_color(me->led,
        lv_color_hex(on ? TOPBAR_LED_ON : TOPBAR_LED_OFF), 0);
}

void UiTopbar_setCount(UiTopbar *me, uint32_t count) {
    SM_REQUIRE(me != NULL);
    SM_REQUIRE(me->container != NULL);
    (void)lv_snprintf(me->blinkyCountText, sizeof me->blinkyCountText, "%lu",
                      (unsigned long)count);
    lv_label_set_text_static(me->blinkyCount, me->blinkyCountText);
}

void UiTopbar_setFps(UiTopbar *me, uint16_t fps) {
    SM_REQUIRE(me != NULL);
    SM_REQUIRE(me->container != NULL);
    SM_REQUIRE(fps <= 999U);
    (void)lv_snprintf(me->fpsText, sizeof me->fpsText, "%uFPS",
                      (unsigned)fps);
    lv_label_set_text_static(me->fps, me->fpsText);
}

void UiTopbar_setCpu(UiTopbar *me, uint8_t percent) {
    SM_REQUIRE(me != NULL);
    SM_REQUIRE(me->container != NULL);
    SM_REQUIRE(percent <= 100U);
    (void)lv_snprintf(me->cpuText, sizeof me->cpuText, "%u%%",
                      (unsigned)percent);
    lv_label_set_text_static(me->cpu, me->cpuText);
}
