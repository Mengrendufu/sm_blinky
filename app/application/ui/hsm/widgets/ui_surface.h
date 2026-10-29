//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef UI_SURFACE_H_
#define UI_SURFACE_H_

#include "lvgl.h"
#include <stdbool.h>

// Owns the content root and one descriptor for the current background;
// borrows verified RGB565 pixels for the lifetime of their presentation.
// Destroy the current page presentation before changing its background.
typedef struct {
    lv_obj_t *root;
    lv_image_dsc_t background;
} UiSurface;

void UiSurface_init(UiSurface *me);
bool UiSurface_showSplash(UiSurface *me, void const *pixels);
void UiSurface_showPage(UiSurface *me, void const *pixels);
struct _lv_obj_t *UiSurface_content(UiSurface *me);
struct _lv_obj_t *UiSurface_overlay(UiSurface *me);

#endif // UI_SURFACE_H_
