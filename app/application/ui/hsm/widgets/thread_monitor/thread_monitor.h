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

// Passive 236x105 monitor skeleton with a clipped 232x83 viewport
// and a 30x6 static dots strip (sibling of the viewport, not inside
// it). Two cards are created once: cards[0] at x=0, cards[1] at
// x=232; each has a header and two resource rows. Three dots at
// x=0/12/24, the first active (green); no pagination, sampling or
// animation yet.
// Zero-initialize before create; the parent must outlive this instance.
// Do not copy/move a live instance or externally delete its subtree.
// UI thread only. Destroy removes only this subtree and is idempotent.
typedef struct {
    lv_obj_t *container;
    lv_obj_t *viewport;
    UiThreadCard cards[2];
    UiDots dots;
} UiThreadMonitor;

void UiThreadMonitor_create(UiThreadMonitor *me, lv_obj_t *parent);
void UiThreadMonitor_destroy(UiThreadMonitor *me);

#endif // THREAD_MONITOR_H_
