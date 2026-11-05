//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef RESOURCE_CARD_H_
#define RESOURCE_CARD_H_

#include "lvgl.h"

// Passive 236x118 resource card: title, gauge and range containers.
// Initial resource values remain "--"; no sampling or ratio math.
// Zero-initialize before create; parent must outlive this instance.
// Do not copy/move a live instance or externally delete its subtree.
// UI thread only. Destroy removes only this subtree and is idempotent.
typedef struct {
    lv_obj_t *container;
    lv_obj_t *header;
    lv_obj_t *sizeGroup;
    lv_obj_t *name;
    lv_obj_t *size;
    lv_obj_t *unit;
    lv_obj_t *gauge;
    lv_obj_t *arc;
    lv_obj_t *percent;
    lv_obj_t *range;
    lv_obj_t *minimum;
    lv_obj_t *maximum;
} UiResourceCard;

// Outer dimensions accept native LVGL pixels or lv_pct().
void UiResourceCard_create(UiResourceCard *me, lv_obj_t *parent,
                            int32_t width, int32_t height);
void UiResourceCard_destroy(UiResourceCard *me);
// All strings are borrowed until rebound/destroyed; UI thread only.
void UiResourceCard_setHeader(UiResourceCard *me, char const *name,
                              char const *size, char const *unit);
void UiResourceCard_setGauge(UiResourceCard *me, char const *percentText,
                             char const *value, char const *total,
                             int32_t percent);

#endif // RESOURCE_CARD_H_
