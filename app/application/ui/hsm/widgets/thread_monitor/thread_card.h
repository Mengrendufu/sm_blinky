//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef THREAD_CARD_H_
#define THREAD_CARD_H_

#include "lvgl.h"
#include <stdint.h>

typedef enum {
    UI_THREAD_CARD_STACK,
    UI_THREAD_CARD_INBOX,
    UI_THREAD_CARD_RESOURCE_COUNT
} UiThreadCardResourceId;

typedef struct {
    lv_obj_t *container;
    lv_obj_t *value;
    lv_obj_t *unit;
    lv_obj_t *bar;
} UiThreadCardResource;

// Passive 232x83 card: a 232x17 header and two 232x23 resource rows,
// separated by 10 px. Each row has value/unit labels and a 232x6 bar.
// All strings are borrowed until rebound or destroyed; keep them
// valid, and repeat the setter after editing a buffer in place.
// Initial text is empty and bars are zero. The caller supplies display
// text and percent (0..100); no sampling, ratio math or animation here.
// The caller controls external placement.
// Zero-initialize before create; the parent must outlive the card.
// Do not copy/move a live instance or externally delete its subtree.
// UI thread only. Destroy removes this subtree and is idempotent.
typedef struct {
    lv_obj_t *container;
    lv_obj_t *header;
    lv_obj_t *threadName;
    lv_obj_t *priority;
    UiThreadCardResource resources[UI_THREAD_CARD_RESOURCE_COUNT];
} UiThreadCard;

void UiThreadCard_create(UiThreadCard *me, lv_obj_t *parent);
void UiThreadCard_destroy(UiThreadCard *me);
void UiThreadCard_setHeader(UiThreadCard *me, char const *threadName,
                            char const *priorityText);
void UiThreadCard_setResource(UiThreadCard *me, UiThreadCardResourceId id,
                              char const *value, char const *unit,
                              int32_t percent);

#endif // THREAD_CARD_H_
