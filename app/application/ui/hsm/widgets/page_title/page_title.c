//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "page_title.h"
#include "widgets/helpers/ui_label.h"
#include "widgets/styles/ui_style.h"
#include "sm_assert.h"

SM_DEFINE_MODULE("page_title")

void UiPageTitle_create(UiPageTitle *me, lv_obj_t *parent,
                        char const *title) {
    SM_REQUIRE(me != NULL);
    SM_REQUIRE(parent != NULL);
    SM_REQUIRE(title != NULL);
    SM_REQUIRE(me->container == NULL);

    lv_obj_t * const box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 236, 17);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
    me->container = box;

    me->title = Label_create(box,
        &(LabelConfig){
            // Compiled Montserrat 14 needs 81 px for
            // "SysMonitor" (Figma box was 79).
            .width = 81, .height = 17,
            .longMode = LV_LABEL_LONG_MODE_CLIP,
            .text = title});
    lv_obj_add_style(me->title, &UiStyle_pageTitleText,
                     LV_PART_MAIN);
}

void UiPageTitle_destroy(UiPageTitle *me) {
    SM_REQUIRE(me != NULL);
    if (me->container != NULL) {
        lv_obj_delete(me->container);
    }
    *me = (UiPageTitle){0};
}

void UiPageTitle_setTitle(UiPageTitle *me, char const *title) {
    SM_REQUIRE(me != NULL);
    SM_REQUIRE(me->container != NULL);
    SM_REQUIRE(title != NULL);
    lv_label_set_text_static(me->title, title);
}
