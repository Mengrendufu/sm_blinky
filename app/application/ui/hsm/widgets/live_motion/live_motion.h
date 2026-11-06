//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef LIVE_MOTION_H_
#define LIVE_MOTION_H_

#include "widgets/arc/arc.h"

// Passive Flex composition; its owning HSM supplies demo progress.
// Zero-initialize before create; UI thread only. Parent must outlive me.
// Do not move/copy live instances or externally delete their subtree.
typedef struct {
    lv_obj_t *container;
    lv_obj_t *header;
    lv_obj_t *heading;
    lv_obj_t *ledStatus;
    lv_obj_t *body;
    UiArc arc;
    lv_obj_t *details;
    lv_obj_t *caption;
    lv_obj_t *value;
    lv_obj_t *bar;
    char text[5];
    uint16_t progress;
} UiLiveMotion;

// Dimensions accept pixels or lv_pct(); initial demo value is zero.
void UiLiveMotion_create(UiLiveMotion *me, lv_obj_t *parent,
                          int32_t width, int32_t height);
// Requires 0..100; updates arc, numeric text and bar without animation.
void UiLiveMotion_setValue(UiLiveMotion *me, uint8_t value);
// Passive display only; no input callbacks or hardware side effects.
void UiLiveMotion_setLedStatus(UiLiveMotion *me, bool on);
// Requires 0..999 per-mille cycle progress; updates the eased arc/bar and
// half-cycle status together. No timer, allocation or hardware access.
void UiLiveMotion_setProgress(UiLiveMotion *me, uint16_t progress);
// Deletes the subtree and clears me; idempotent when empty.
void UiLiveMotion_destroy(UiLiveMotion *me);

#endif // LIVE_MOTION_H_
