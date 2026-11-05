//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef UI_ROOT_H_
#define UI_ROOT_H_

#include "lvgl.h"
#include <stdbool.h>

// Owns the sole content container under the active screen and one
// descriptor for its current background; borrows verified RGB565
// pixels for the lifetime of their presentation.
typedef struct {
    lv_obj_t *container;
    lv_image_dsc_t background;
} UiRoot;

void UiRoot_init(UiRoot *me);
void UiRoot_setBackground(UiRoot *me, void const *pixels);

#endif // UI_ROOT_H_
