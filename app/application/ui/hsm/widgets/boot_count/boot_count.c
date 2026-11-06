//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "boot_count.h"
#include "widgets/helpers/ui_flex.h"
#include "widgets/helpers/ui_label.h"
#include "widgets/styles/ui_style.h"
#include "sm_assert.h"
#include <string.h>

SM_DEFINE_MODULE("boot_count")

void UiBootCount_create(UiBootCount *me, lv_obj_t *parent,
                         int32_t width, int32_t height) {
    SM_REQUIRE(me != NULL);
    SM_REQUIRE(parent != NULL);
    SM_REQUIRE(me->container == NULL);

    lv_obj_t * const meta = FlexContainer_create(parent,
        &(UiFlexConfig){
            .width = width, .height = height, .flow = LV_FLEX_FLOW_ROW,
            .mainPlace = LV_FLEX_ALIGN_SPACE_EVENLY,
            .crossPlace = LV_FLEX_ALIGN_CENTER,
            .trackPlace = LV_FLEX_ALIGN_START,
            .padTop = 0, .padBottom = 0, .padLeft = 0,
            .padRight = 0, .padRow = 0, .padColumn = 2});
    me->container = meta;

    lv_obj_t * const boots = Label_create(meta,
        &(LabelConfig){
            .width = 52, .height = 17,
            .longMode = LV_LABEL_LONG_MODE_CLIP,
            .text = "BOOTS"});
    lv_obj_add_style(boots, &UiStyle_bootCountText, LV_PART_MAIN);
    me->count = Label_create(meta,
        &(LabelConfig){
            .width = 97, .height = 17,
            .textAlign = LV_TEXT_ALIGN_RIGHT,
            .longMode = LV_LABEL_LONG_MODE_CLIP,
            .text = me->countText});
    lv_obj_add_style(me->count, &UiStyle_bootCountText,
                     LV_PART_MAIN);

    UiBootCount_setCount(me, 0U);
}

void UiBootCount_destroy(UiBootCount *me) {
    SM_REQUIRE(me != NULL);
    if (me->container != NULL) {
        lv_obj_delete(me->container);
    }
    *me = (UiBootCount){0};
}

void UiBootCount_setCount(UiBootCount *me, uint32_t count) {
    SM_REQUIRE(me != NULL);
    SM_REQUIRE(me->container != NULL);
    char digits[11];
    (void)lv_snprintf(digits, sizeof digits, "%lu",
                      (unsigned long)count);
    size_t const len = strlen(digits);
    char *dst = me->countText;
    for (size_t i = 0; i < len; ++i) {
        if (i != 0U && (len - i) % 3U == 0U) {
            *dst++ = ',';
        }
        *dst++ = digits[i];
    }
    *dst = '\0';
    lv_label_set_text_static(me->count, me->countText);
}
