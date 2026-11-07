//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef UI_LIVE_SHOW_H_
#define UI_LIVE_SHOW_H_

#include "widgets/ui_root/ui_root.h"
#include "widgets/page_title/page_title.h"
#include "widgets/live_motion/live_motion.h"
#include "widgets/live_signal/live_signal.h"

// Stateless LiveShow-page visual operations; UI thread only.
// The caller owns the initialized root and the component instances.
// build requires empty components and borrows pixels as the background
// (NULL is a black fallback). The root's common COLUMN layout is retained.
// hide destroys Signal, Motion and title, then clears the background;
// idempotent when already empty. Root, Topbar and siblings survive.
// No state transitions, timing, sampling or autonomous animation.
void UiLiveShow_build(UiRoot *root, UiPageTitle *pageTitle,
                      UiLiveMotion *motion, UiLiveSignal *signal,
                      void const *pixels);
void UiLiveShow_hide(UiRoot *root, UiPageTitle *pageTitle,
                     UiLiveMotion *motion, UiLiveSignal *signal);

#endif // UI_LIVE_SHOW_H_
