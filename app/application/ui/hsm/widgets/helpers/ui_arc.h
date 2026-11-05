//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef UI_ARC_H_
#define UI_ARC_H_

#include "lvgl.h"
#include <stdint.h>

// Creates one style-free, passive lv_arc, without labels or a knob.
// parent/config must be non-NULL; minValue < maxValue and the initial
// value must be inside that range. Angles are clockwise degrees 0..360;
// startAngle > endAngle crosses zero, as in native LVGL.
// Dimensions are forwarded unchanged, including percent sizes.
// Normal mode, zero rotation; configuration is borrowed only during
// creation. Position belongs to the parent layout/caller. Appearance
// is attached separately to MAIN (track) and INDICATOR.
// Update via lv_arc_set_value/range/bg_angles afterwards.
// LVGL/parent owns the result: delete directly or with its parent.
// UI thread only; LVGL's coordinate/range arithmetic limits still apply.
typedef struct {
    int32_t width;
    int32_t height;
    uint16_t startAngle;
    uint16_t endAngle;
    int32_t minValue;
    int32_t maxValue;
    int32_t value;
} ArcConfig;

lv_obj_t *Arc_create(lv_obj_t *parent, ArcConfig const *config);

#endif // UI_ARC_H_
