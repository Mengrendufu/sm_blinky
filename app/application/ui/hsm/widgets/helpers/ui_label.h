//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef UI_LABEL_H_
#define UI_LABEL_H_

#include "lvgl.h"
#include <stdint.h>

// Shared label creation for HSM widgets: one real lv_label child of
// the given parent, style-free, with the configured size, long mode
// and static text. Appearance (font/color) is NOT part of this
// config: attach a shared const style (hsm/widgets/styles/ui_style.h) after
// creation with lv_obj_add_style if needed.
//
// The config is borrowed for the duration of the call only; nothing
// is retained, so automatic storage and compound literals are fine.
// parent, config and text must be non-NULL (SM_REQUIRE).
// text IS borrowed and must outlive the label until it is rebound
// or deleted; it is lent via lv_label_set_text_static -- no copy is
// made, so after editing the buffer in place the caller must notify
// LVGL by calling lv_label_set_text_static(label, text) again.
//
// LV_LABEL_LONG_MODE_DOTS is rejected: this LVGL does not support
// dots with static text (it only logs a warning and does not render
// them), so requiring it here would silently mislead. Other modes,
// LV_SIZE_CONTENT and lv_pct sizes are forwarded unchanged.
//
// textAlign is the horizontal alignment of the text INSIDE the
// label box: LV_TEXT_ALIGN_AUTO (the zero default), LEFT, CENTER,
// or RIGHT; it has no vertical or object-position effects and is
// independent of ignoreLayout below.
//
// ignoreLayout is the opt-in OBJECT position switch: when NULL
// (the zero default) the helper writes NO object position or
// alignment at all -- a Flex/Grid parent owns the label's spot
// exactly like any LVGL
// object. When non-NULL the helper sets LV_OBJ_FLAG_IGNORE_LAYOUT
// and aligns TOP_LEFT to the given x/y, which are relative to the
// parent CONTENT area top-left (inside its border and padding), not
// the screen; signed values and lv_pct are forwarded, and the label
// follows when the parent moves. The config AND the pointee are
// borrowed for the call only; neither is retained.
// Delete with lv_obj_delete or via the parent's lifecycle; flags and
// position may still be changed afterwards with plain LVGL calls.
// UI thread only.
typedef struct {
    int32_t x;
    int32_t y;
} LabelIgnoreLayout;

typedef struct {
    int32_t width;
    int32_t height;
    lv_label_long_mode_t longMode;
    lv_text_align_t textAlign;
    char const *text;
    LabelIgnoreLayout const *ignoreLayout;
} LabelConfig;

lv_obj_t *Label_create(lv_obj_t *parent, LabelConfig const *config);

#endif // UI_LABEL_H_
