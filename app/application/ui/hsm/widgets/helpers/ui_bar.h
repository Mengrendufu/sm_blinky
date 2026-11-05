//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef UI_BAR_H_
#define UI_BAR_H_

#include "lvgl.h"
#include <stdint.h>

// Creates one style-free, passive lv_bar (no child objects or labels).
// parent/config must be non-NULL; minValue < maxValue and the initial
// value must be inside that ascending range. Dimensions are forwarded
// unchanged, including percent sizes. Normal mode and AUTO orientation
// use LVGL's defaults; initial value is applied without animation.
// Config is borrowed only during this call. Position belongs to the
// parent layout/caller; appearance is attached separately to MAIN
// (track) and INDICATOR. Update via lv_bar_set_value/range afterwards.
// LVGL/parent owns the result: delete it directly or with its parent.
// UI thread only; LVGL's coordinate/range arithmetic limits still apply.
typedef struct {
    int32_t width;
    int32_t height;
    int32_t minValue;
    int32_t maxValue;
    int32_t value;
} BarConfig;

lv_obj_t *Bar_create(lv_obj_t *parent, BarConfig const *config);

#endif // UI_BAR_H_
