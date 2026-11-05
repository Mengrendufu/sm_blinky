//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef PAGE_TITLE_H_
#define PAGE_TITLE_H_

#include "lvgl.h"

// Passive page-title strip: a plain 236x17 container with a left
// title label. The caller composes further child components under
// the container and MUST destroy those child instances BEFORE
// destroying the PageTitle (ordinary LVGL parent lifetime).
// Zero-initialize the instance and create once under a parent that
// outlives it; never copy or move a live instance. The title string
// is BORROWED (bound via lv_label_set_text_static): keep it valid
// until the title is rebound or the component is destroyed.
// Appearance comes from the shared const UiStyle_pageTitleText.
// UI thread only; destroy is idempotent when empty and removes only
// the component's own subtree.
typedef struct {
    lv_obj_t *container;
    lv_obj_t *title;
} UiPageTitle;

void UiPageTitle_create(UiPageTitle *me, lv_obj_t *parent,
                        char const *title);
void UiPageTitle_destroy(UiPageTitle *me);
void UiPageTitle_setTitle(UiPageTitle *me, char const *title);

#endif // PAGE_TITLE_H_
