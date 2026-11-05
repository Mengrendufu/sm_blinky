//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef BOOT_COUNT_H_
#define BOOT_COUNT_H_

#include "lvgl.h"
#include <stdint.h>

// Passive boot-count group: a 106x12 flex row holding the fixed
// "BOOTS" caption and a comma-formatted count label. The caller
// composes it under a parent container and positions it (this
// factory does not align it externally). Zero-initialize the
// instance and create once under a parent that outlives it; never
// copy or move a live instance. countText is owned by the instance.
// Appearance comes from the shared const UiStyle_bootCountText.
// UI thread only; destroy is idempotent when empty and removes only
// the component's own subtree.
typedef struct {
    lv_obj_t *container;
    lv_obj_t *count;
    char countText[14]; // "4,294,967,295"
} UiBootCount;

// Outer dimensions accept native LVGL pixels or lv_pct().
void UiBootCount_create(UiBootCount *me, lv_obj_t *parent,
                         int32_t width, int32_t height);
void UiBootCount_destroy(UiBootCount *me);
void UiBootCount_setCount(UiBootCount *me, uint32_t count);

#endif // BOOT_COUNT_H_
