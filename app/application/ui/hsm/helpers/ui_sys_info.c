//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "ui_sys_info.h"
#include "sm_assert.h"

SM_DEFINE_MODULE("ui_sys_info")

void UiSysInfo_build(UiRoot *root, UiPageTitle *pageTitle,
                     UiBootCount *bootCount, UiThreadMonitor *monitor,
                     UiResourceMonitor *resourceMonitor,
                     void const *pixels) {
    SM_REQUIRE(root != NULL);
    SM_REQUIRE(root->container != NULL);
    SM_REQUIRE(pageTitle != NULL);
    SM_REQUIRE(pageTitle->container == NULL);
    SM_REQUIRE(bootCount != NULL);
    SM_REQUIRE(bootCount->container == NULL);
    SM_REQUIRE(monitor != NULL);
    SM_REQUIRE(monitor->container == NULL);
    SM_REQUIRE(resourceMonitor != NULL);
    SM_REQUIRE(resourceMonitor->container == NULL);
    UiRoot_setBackground(root, pixels);
    UiPageTitle_create(pageTitle, root->container, "SysMonitor");
    UiBootCount_create(bootCount, pageTitle->container);
    lv_obj_align(bootCount->container, LV_ALIGN_RIGHT_MID, 0, 0);
    UiThreadMonitor_create(monitor, root->container);
    // Figma outer frame (2, 58.57) rounds to (2, 59). Existing shared
    // strips yield (0, 57); margins preserve Flex and their positions.
    lv_obj_set_style_margin_left(monitor->container, 2, LV_PART_MAIN);
    lv_obj_set_style_margin_top(monitor->container, 2, LV_PART_MAIN);
    UiThreadCard_setHeader(&monitor->cards[0], "Thread", "--");
    UiThreadCard_setResource(&monitor->cards[0], UI_THREAD_CARD_STACK,
                             "--", "B", 0);
    UiThreadCard_setResource(&monitor->cards[0], UI_THREAD_CARD_INBOX,
                             "--", "slots", 0);
    UiResourceMonitor_create(resourceMonitor, root->container);
    lv_obj_set_style_margin_left(resourceMonitor->container, 2,
                                 LV_PART_MAIN);
}

void UiSysInfo_hide(UiRoot *root, UiPageTitle *pageTitle,
                    UiBootCount *bootCount, UiThreadMonitor *monitor,
                    UiResourceMonitor *resourceMonitor) {
    SM_REQUIRE(root != NULL);
    SM_REQUIRE(root->container != NULL);
    SM_REQUIRE(pageTitle != NULL);
    SM_REQUIRE(bootCount != NULL);
    SM_REQUIRE(monitor != NULL);
    SM_REQUIRE(resourceMonitor != NULL);
    UiResourceMonitor_destroy(resourceMonitor);
    UiThreadMonitor_destroy(monitor);
    UiBootCount_destroy(bootCount);
    UiPageTitle_destroy(pageTitle);
    UiRoot_setBackground(root, NULL);
}
