//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef UI_DOTS_H_
#define UI_DOTS_H_

#include "lvgl.h"

// Passive indicator group: count 6x6 dots, gap 6, first active.
// count must be nonzero; group width is count * 12 - 6.
// The component owns its whole subtree; individual dots are internal.
// Caller controls external placement; no pagination or animation yet.
// Zero-initialize before create; parent must outlive this instance.
// Do not copy/move a live instance or externally delete its subtree.
// UI thread only. Destroy is idempotent when already empty.
typedef struct {
    lv_obj_t *container;
} UiDots;

void UiDots_create(UiDots *me, lv_obj_t *parent, uint8_t count);
void UiDots_destroy(UiDots *me);

#endif // UI_DOTS_H_
