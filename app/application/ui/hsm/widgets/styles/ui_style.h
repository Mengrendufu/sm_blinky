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

// Shared const styles for HSM widgets: appearance and fixed geometry.
// Dynamic offsets and caller-selected outer dimensions stay local.
// Components attach a style after creation with
// lv_obj_add_style(obj, &UiStyle_*, LV_PART_MAIN). Const styles live
// for the program lifetime and need no cleanup; attaching to an
// object is still an LVGL style reference (LVGL keeps per-object
// style metadata). UI thread only.
extern lv_style_t const UiStyle_topbarText;
extern lv_style_t const UiStyle_pageTitleText;
extern lv_style_t const UiStyle_bootCountText;
extern lv_style_t const UiStyle_barTrack;
extern lv_style_t const UiStyle_barIndicator;
extern lv_style_t const UiStyle_dot;
extern lv_style_t const UiStyle_dotActive;
extern lv_style_t const UiStyle_arcTrack;
extern lv_style_t const UiStyle_arcIndicator;
extern lv_style_t const UiStyle_signalWave;
extern lv_style_t const UiStyle_signalPhase;
extern lv_style_t const UiStyle_signalPhaseActive;
extern lv_style_t const UiStyle_threadHeader;
extern lv_style_t const UiStyle_threadResourceRow;
extern lv_style_t const UiStyle_threadName;
extern lv_style_t const UiStyle_threadPriority;
extern lv_style_t const UiStyle_threadValue;
extern lv_style_t const UiStyle_threadUnit;
extern lv_style_t const UiStyle_resourceHeader;
extern lv_style_t const UiStyle_resourceSizeGroup;
extern lv_style_t const UiStyle_resourceGauge;
extern lv_style_t const UiStyle_resourceRange;
extern lv_style_t const UiStyle_resourceName;
extern lv_style_t const UiStyle_resourceSize;
extern lv_style_t const UiStyle_resourceUnit;
extern lv_style_t const UiStyle_resourcePercent;
extern lv_style_t const UiStyle_resourceMinimum;
extern lv_style_t const UiStyle_resourceMaximum;
extern lv_style_t const UiStyle_liveLabel;
extern lv_style_t const UiStyle_signalLabel;
extern lv_style_t const UiStyle_livePercent;
extern lv_style_t const UiStyle_switchTrack;
extern lv_style_t const UiStyle_switchIndicator;
extern lv_style_t const UiStyle_switchChecked;
extern lv_style_t const UiStyle_switchKnob;

#endif // UI_STYLE_H_
