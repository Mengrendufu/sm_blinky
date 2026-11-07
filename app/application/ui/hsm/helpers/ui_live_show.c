//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "ui_live_show.h"
#include "sm_assert.h"

SM_DEFINE_MODULE("ui_live_show")

void UiLiveShow_build(UiRoot *root, UiPageTitle *pageTitle,
                      UiLiveMotion *motion, UiLiveSignal *signal,
                      void const *pixels) {
    SM_REQUIRE(root != NULL);
    SM_REQUIRE(root->container != NULL);
    SM_REQUIRE(pageTitle != NULL);
    SM_REQUIRE(pageTitle->container == NULL);
    SM_REQUIRE(motion != NULL);
    SM_REQUIRE(motion->container == NULL);
    SM_REQUIRE(signal != NULL);
    SM_REQUIRE(signal->container == NULL);
    UiRoot_setBackground(root, pixels);
    UiPageTitle_create(pageTitle, root->container,
                       lv_pct(100), 17, "LiveShow");
    lv_obj_set_style_margin_top(pageTitle->container, 1, LV_PART_MAIN);
    UiLiveMotion_create(motion, root->container, lv_pct(100), 112);
    UiLiveSignal_create(signal, root->container, lv_pct(100), 133);
}

void UiLiveShow_hide(UiRoot *root, UiPageTitle *pageTitle,
                     UiLiveMotion *motion, UiLiveSignal *signal) {
    SM_REQUIRE(root != NULL);
    SM_REQUIRE(root->container != NULL);
    SM_REQUIRE(pageTitle != NULL);
    SM_REQUIRE(motion != NULL);
    SM_REQUIRE(signal != NULL);
    UiLiveSignal_destroy(signal);
    UiLiveMotion_destroy(motion);
    UiPageTitle_destroy(pageTitle);
    UiRoot_setBackground(root, NULL);
}
