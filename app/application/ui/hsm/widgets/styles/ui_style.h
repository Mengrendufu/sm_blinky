//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef UI_STYLE_H_
#define UI_STYLE_H_

#include "lvgl.h"

// Shared const styles for HSM widgets. Appearance lives here, not in
// the layout helpers; components attach a style after creation with
// lv_obj_add_style(obj, &UiStyle_*, LV_PART_MAIN). Const styles live
// for the program lifetime and need no cleanup; attaching to an
// object is still an LVGL style reference (LVGL keeps per-object
// style metadata). UI thread only.
extern lv_style_t const UiStyle_topbarText;
extern lv_style_t const UiStyle_pageTitleText;
extern lv_style_t const UiStyle_bootCountText;
extern lv_style_t const UiStyle_threadCardText;
extern lv_style_t const UiStyle_barTrack;
extern lv_style_t const UiStyle_barIndicator;
extern lv_style_t const UiStyle_dot;
extern lv_style_t const UiStyle_dotActive;
extern lv_style_t const UiStyle_resourceCardText;
extern lv_style_t const UiStyle_arcTrack;
extern lv_style_t const UiStyle_arcIndicator;
extern lv_style_t const UiStyle_liveText;
extern lv_style_t const UiStyle_signalText;
extern lv_style_t const UiStyle_signalWave;
extern lv_style_t const UiStyle_signalPhase;
extern lv_style_t const UiStyle_signalPhaseActive;

#endif // UI_STYLE_H_
