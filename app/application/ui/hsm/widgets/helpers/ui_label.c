//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "ui_label.h"
#include "sm_assert.h"

SM_DEFINE_MODULE("ui_label")

lv_obj_t *Label_create(lv_obj_t *parent, LabelConfig const *config) {
    SM_REQUIRE(parent != NULL);
    SM_REQUIRE(config != NULL);
    SM_REQUIRE(config->text != NULL);
    SM_REQUIRE(config->longMode != LV_LABEL_LONG_MODE_DOTS);
    SM_REQUIRE(config->textAlign == LV_TEXT_ALIGN_AUTO
               || config->textAlign == LV_TEXT_ALIGN_LEFT
               || config->textAlign == LV_TEXT_ALIGN_CENTER
               || config->textAlign == LV_TEXT_ALIGN_RIGHT);

    lv_obj_t * const label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_label_set_long_mode(label, config->longMode);
    lv_obj_set_size(label, config->width, config->height);
    lv_obj_set_style_text_align(label, config->textAlign,
                                LV_PART_MAIN);
    if (config->ignoreLayout != NULL) {
        lv_obj_add_flag(label, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_align(label, LV_ALIGN_TOP_LEFT,
                     config->ignoreLayout->x,
                     config->ignoreLayout->y);
    }
    lv_label_set_text_static(label, config->text);
    return label;
}
