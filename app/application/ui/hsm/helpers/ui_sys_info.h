//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef UI_SYS_INFO_H_
#define UI_SYS_INFO_H_

#include "widgets/ui_root/ui_root.h"
#include "widgets/page_title/page_title.h"
#include "widgets/boot_count/boot_count.h"
#include "widgets/thread_monitor/thread_monitor.h"
#include "widgets/resource_monitor/resource_monitor.h"

// Stateless SysInfo-page visual operations: the caller (Sm_Ui) owns
// the initialized root (UiRoot_init owns its common COLUMN layout)
// plus the zeroed/living PageTitle, BootCount and monitor
// instances. This helper does NOT own the root and cannot destroy
// it, the shared Topbar, or any sibling; no state, timing, or NVM.
// build borrows the given pixels as the root background (NULL is a
// legitimate black fallback), mounts the page title, then composes
// the boot-count group under it, the thread monitor below, and the
// resource skeleton last. All must be empty.
// hide destroys the resource monitor, thread monitor, boot count and
// title, then clears the bg; it is idempotent when already empty.
// UI thread only.
void UiSysInfo_build(UiRoot *root, UiPageTitle *pageTitle,
                     UiBootCount *bootCount, UiThreadMonitor *monitor,
                     UiResourceMonitor *resourceMonitor,
                     void const *pixels);
void UiSysInfo_hide(UiRoot *root, UiPageTitle *pageTitle,
                    UiBootCount *bootCount, UiThreadMonitor *monitor,
                    UiResourceMonitor *resourceMonitor);

#endif // UI_SYS_INFO_H_
