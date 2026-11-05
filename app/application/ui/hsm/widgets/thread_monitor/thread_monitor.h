//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef THREAD_MONITOR_H_
#define THREAD_MONITOR_H_

#include "thread_card.h"
#include "widgets/dots/dots.h"

// Passive monitor with a clipped viewport and a sibling dots strip.
// Two cards are created once at x=0 and x=100% of the viewport.
// pageCount controls the dot count, not the number of reusable cards.
// The caller owns sampling, rebinding and placement during pagination.
// Zero-initialize before create; the parent must outlive this instance.
// Do not copy/move a live instance or externally delete its subtree.
// UI thread only. Destroy removes only this subtree and is idempotent.
typedef struct {
    lv_obj_t *container;
    lv_obj_t *viewport;
    UiThreadCard cards[2];
    UiDots dots;
    uint8_t slideFrom;
    uint8_t slideTo;
    bool sliding;
} UiThreadMonitor;

// Outer dimensions accept native LVGL pixels or lv_pct().
void UiThreadMonitor_create(UiThreadMonitor *me, lv_obj_t *parent,
                             int32_t width, int32_t height,
                             uint8_t pageCount);
void UiThreadMonitor_destroy(UiThreadMonitor *me);
// Slide a bound incoming slot from the right while the current slot exits
// left. Both slots must be distinct and in 0..1; no slide may be in progress.
// Completion parks the outgoing card at x=100%. Destroy cancels animation.
void UiThreadMonitor_slide(UiThreadMonitor *me, uint8_t from, uint8_t to);

#endif // THREAD_MONITOR_H_
