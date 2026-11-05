//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef UI_FLEX_H_
#define UI_FLEX_H_

#include "lvgl.h"
#include <stdint.h>

// Shared flex-layout object creation for HSM widgets: one ordinary
// style-free transparent child of the given parent, sized and padded
// as configured. padTop/Bottom/Left/Right are the object's own inner
// edge pads; padRow/padColumn are the gaps between flex tracks/items.
// width/height/pads are forwarded as LVGL int32 values, including
// LV_SIZE_CONTENT or percent sizes -- no range imposed here.
//
// The config is borrowed for the duration of the call only; nothing
// is retained, so automatic storage and compound literals are fine.
// parent and config must be non-NULL (parent NULL is rejected: this
// is not a screen creator). LVGL/parent owns the returned object;
// delete it with lv_obj_delete or via the parent's lifecycle.
// UI thread only; LV_USE_FLEX is already enabled.
typedef struct {
    int32_t width;
    int32_t height;
    lv_flex_flow_t flow;
    lv_flex_align_t mainPlace;
    lv_flex_align_t crossPlace;
    lv_flex_align_t trackPlace;
    int32_t padTop;
    int32_t padBottom;
    int32_t padLeft;
    int32_t padRight;
    int32_t padRow;
    int32_t padColumn;
} UiFlexConfig;

lv_obj_t *FlexContainer_create(lv_obj_t *parent, UiFlexConfig const *config);

#endif // UI_FLEX_H_
