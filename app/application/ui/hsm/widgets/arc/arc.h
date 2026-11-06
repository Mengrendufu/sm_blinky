//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef ARC_H_
#define ARC_H_

#include "lvgl.h"
#include <stdint.h>

// Passive 270-degree percentage arc; container is the arc itself.
// Zero-initialize before create. UI thread only; parent must outlive me.
// Do not move/copy a live instance or externally delete its subtree:
// the label borrows this instance's text. No sampling or animation.
typedef struct {
    lv_obj_t *container;
    lv_obj_t *percent;
    char text[5];
} UiArc;

// Dimensions accept pixels or lv_pct(); placement belongs to the parent.
// Starts at 0%; creates only one arc and its centered label.
void UiArc_create(UiArc *me, lv_obj_t *parent,
                   int32_t width, int32_t height);
// Requires a live instance and value in 0..100; updates arc and label.
void UiArc_setValue(UiArc *me, uint8_t value);
// Deletes this subtree, clears the instance; safe to repeat when empty.
void UiArc_destroy(UiArc *me);

#endif // ARC_H_
