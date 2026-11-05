//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef RESOURCE_MONITOR_H_
#define RESOURCE_MONITOR_H_

#include "resource_card.h"
#include "widgets/dots/dots.h"

// Passive 236x144 resource slot: clipped 236x118 viewport, two
// reusable cards and a sibling indicator container owning UiDots.
// The card positions are (0,0) and (236,0) inside the viewport.
// No resource binding, pagination or animation.
// Zero-initialize before create; parent must outlive this instance.
// Do not copy/move a live instance or externally delete its subtree.
// UI thread only. Destroy removes only this subtree and is idempotent.
typedef struct {
    lv_obj_t *container;
    lv_obj_t *viewport;
    lv_obj_t *indicatorContainer;
    UiResourceCard cards[2];
    UiDots dots;
} UiResourceMonitor;

// Outer dimensions accept native LVGL pixels or lv_pct().
void UiResourceMonitor_create(UiResourceMonitor *me, lv_obj_t *parent,
                               int32_t width, int32_t height);
void UiResourceMonitor_destroy(UiResourceMonitor *me);

#endif // RESOURCE_MONITOR_H_
