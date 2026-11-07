//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "ui_style.h"

static lv_style_const_prop_t const UiStyle_topbarTextProps_[] = {
    {.prop = LV_STYLE_TEXT_FONT,
     .value = {.ptr = &lv_font_montserrat_14}},
    {.prop = LV_STYLE_TEXT_COLOR,
     .value = {.color = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF)}},
    LV_STYLE_CONST_PROPS_END
};

LV_STYLE_CONST_INIT(UiStyle_topbarText, UiStyle_topbarTextProps_);

static lv_style_const_prop_t const UiStyle_pageTitleTextProps_[] = {
    {.prop = LV_STYLE_TEXT_FONT,
     .value = {.ptr = &lv_font_montserrat_14}},
    {.prop = LV_STYLE_TEXT_COLOR,
     .value = {.color = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF)}},
    LV_STYLE_CONST_PROPS_END
};

LV_STYLE_CONST_INIT(UiStyle_pageTitleText,
                    UiStyle_pageTitleTextProps_);

static lv_style_const_prop_t const
    UiStyle_bootCountTextProps_[] = {
    {.prop = LV_STYLE_TEXT_FONT,
     .value = {.ptr = &lv_font_montserrat_14}},
    {.prop = LV_STYLE_TEXT_COLOR,
     .value = {.color = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF)}},
    LV_STYLE_CONST_PROPS_END
};

LV_STYLE_CONST_INIT(UiStyle_bootCountText,
                    UiStyle_bootCountTextProps_);

static lv_style_const_prop_t const UiStyle_barTrackProps_[] = {
    {.prop = LV_STYLE_BG_COLOR,
     .value = {.color = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF)}},
    {.prop = LV_STYLE_BG_OPA, .value = {.num = LV_OPA_30}},
    {.prop = LV_STYLE_RADIUS, .value = {.num = LV_RADIUS_CIRCLE}},
    LV_STYLE_CONST_PROPS_END
};

LV_STYLE_CONST_INIT(UiStyle_barTrack, UiStyle_barTrackProps_);

static lv_style_const_prop_t const UiStyle_barIndicatorProps_[] = {
    {.prop = LV_STYLE_BG_COLOR,
     .value = {.color = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF)}},
    {.prop = LV_STYLE_BG_OPA, .value = {.num = LV_OPA_COVER}},
    {.prop = LV_STYLE_RADIUS, .value = {.num = LV_RADIUS_CIRCLE}},
    LV_STYLE_CONST_PROPS_END
};

LV_STYLE_CONST_INIT(UiStyle_barIndicator, UiStyle_barIndicatorProps_);

static lv_style_const_prop_t const
    UiStyle_dotProps_[] = {
    {.prop = LV_STYLE_BG_COLOR,
     .value = {.color = LV_COLOR_MAKE(0x33, 0x41, 0x55)}},
    {.prop = LV_STYLE_BG_OPA, .value = {.num = LV_OPA_COVER}},
    {.prop = LV_STYLE_RADIUS, .value = {.num = LV_RADIUS_CIRCLE}},
    LV_STYLE_CONST_PROPS_END
};

LV_STYLE_CONST_INIT(UiStyle_dot, UiStyle_dotProps_);

static lv_style_const_prop_t const
    UiStyle_dotActiveProps_[] = {
    {.prop = LV_STYLE_BG_COLOR,
     .value = {.color = LV_COLOR_MAKE(0x6F, 0xE9, 0x7B)}},
    LV_STYLE_CONST_PROPS_END
};

LV_STYLE_CONST_INIT(UiStyle_dotActive, UiStyle_dotActiveProps_);

static lv_style_const_prop_t const UiStyle_arcTrackProps_[] = {
    {.prop = LV_STYLE_ARC_WIDTH, .value = {.num = 6}},
    {.prop = LV_STYLE_ARC_ROUNDED, .value = {.num = 1}},
    {.prop = LV_STYLE_ARC_OPA, .value = {.num = LV_OPA_COVER}},
    {.prop = LV_STYLE_ARC_COLOR,
     .value = {.color = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF)}},
    LV_STYLE_CONST_PROPS_END
};

LV_STYLE_CONST_INIT(UiStyle_arcTrack, UiStyle_arcTrackProps_);

static lv_style_const_prop_t const
    UiStyle_arcIndicatorProps_[] = {
    {.prop = LV_STYLE_ARC_WIDTH, .value = {.num = 6}},
    {.prop = LV_STYLE_ARC_ROUNDED, .value = {.num = 1}},
    {.prop = LV_STYLE_ARC_OPA, .value = {.num = LV_OPA_COVER}},
    {.prop = LV_STYLE_ARC_COLOR,
     .value = {.color = LV_COLOR_MAKE(0xBE, 0xE0, 0x86)}},
    LV_STYLE_CONST_PROPS_END
};

LV_STYLE_CONST_INIT(UiStyle_arcIndicator, UiStyle_arcIndicatorProps_);

static lv_style_const_prop_t const UiStyle_signalWaveProps_[] = {
    {.prop = LV_STYLE_WIDTH, .value = {.num = 202}},
    {.prop = LV_STYLE_HEIGHT, .value = {.num = 40}},
    {.prop = LV_STYLE_LINE_WIDTH, .value = {.num = 2}},
    {.prop = LV_STYLE_LINE_ROUNDED, .value = {.num = 1}},
    {.prop = LV_STYLE_LINE_OPA, .value = {.num = LV_OPA_COVER}},
    {.prop = LV_STYLE_LINE_COLOR,
     .value = {.color = LV_COLOR_MAKE(0xBE, 0xE0, 0x86)}},
    LV_STYLE_CONST_PROPS_END
};
LV_STYLE_CONST_INIT(UiStyle_signalWave, UiStyle_signalWaveProps_);

static lv_style_const_prop_t const UiStyle_signalPhaseProps_[] = {
    {.prop = LV_STYLE_WIDTH, .value = {.num = 20}},
    {.prop = LV_STYLE_HEIGHT, .value = {.num = 4}},
    {.prop = LV_STYLE_BG_OPA, .value = {.num = LV_OPA_COVER}},
    {.prop = LV_STYLE_BG_COLOR,
     .value = {.color = LV_COLOR_MAKE(0x81, 0x94, 0x80)}},
    LV_STYLE_CONST_PROPS_END
};
LV_STYLE_CONST_INIT(UiStyle_signalPhase, UiStyle_signalPhaseProps_);

static lv_style_const_prop_t const UiStyle_signalPhaseActiveProps_[] = {
    {.prop = LV_STYLE_BG_COLOR,
     .value = {.color = LV_COLOR_MAKE(0x61, 0xDF, 0x5A)}},
    LV_STYLE_CONST_PROPS_END
};
LV_STYLE_CONST_INIT(UiStyle_signalPhaseActive,
                    UiStyle_signalPhaseActiveProps_);

// Every fixed label uses the same typography, but owns no local style.
#define UI_TEXT_PROPS_(w, a) \
    {.prop = LV_STYLE_WIDTH, .value = {.num = (w)}}, \
    {.prop = LV_STYLE_HEIGHT, .value = {.num = 17}}, \
    {.prop = LV_STYLE_TEXT_ALIGN, .value = {.num = (a)}}, \
    {.prop = LV_STYLE_TEXT_FONT, .value = {.ptr = &lv_font_montserrat_14}}, \
    {.prop = LV_STYLE_TEXT_COLOR, \
     .value = {.color = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF)}}

static lv_style_const_prop_t const UiStyle_threadNameProps_[] = {
    UI_TEXT_PROPS_(LV_PCT(75), LV_TEXT_ALIGN_LEFT),
    {.prop = LV_STYLE_ALIGN, .value = {.num = LV_ALIGN_LEFT_MID}},
    LV_STYLE_CONST_PROPS_END
};
LV_STYLE_CONST_INIT(UiStyle_threadName, UiStyle_threadNameProps_);

static lv_style_const_prop_t const UiStyle_threadPriorityProps_[] = {
    UI_TEXT_PROPS_(LV_PCT(25), LV_TEXT_ALIGN_RIGHT),
    {.prop = LV_STYLE_ALIGN, .value = {.num = LV_ALIGN_RIGHT_MID}},
    LV_STYLE_CONST_PROPS_END
};
LV_STYLE_CONST_INIT(UiStyle_threadPriority, UiStyle_threadPriorityProps_);

static lv_style_const_prop_t const UiStyle_threadValueProps_[] = {
    UI_TEXT_PROPS_(LV_PCT(75), LV_TEXT_ALIGN_LEFT),
    {.prop = LV_STYLE_ALIGN, .value = {.num = LV_ALIGN_TOP_LEFT}},
    LV_STYLE_CONST_PROPS_END
};
LV_STYLE_CONST_INIT(UiStyle_threadValue, UiStyle_threadValueProps_);

static lv_style_const_prop_t const UiStyle_threadUnitProps_[] = {
    UI_TEXT_PROPS_(LV_PCT(25), LV_TEXT_ALIGN_RIGHT),
    {.prop = LV_STYLE_ALIGN, .value = {.num = LV_ALIGN_TOP_RIGHT}},
    LV_STYLE_CONST_PROPS_END
};
LV_STYLE_CONST_INIT(UiStyle_threadUnit, UiStyle_threadUnitProps_);

static lv_style_const_prop_t const UiStyle_resourceNameProps_[] = {
    UI_TEXT_PROPS_(174, LV_TEXT_ALIGN_LEFT),
    {.prop = LV_STYLE_ALIGN, .value = {.num = LV_ALIGN_TOP_LEFT}},
    {.prop = LV_STYLE_X, .value = {.num = 2}},
    LV_STYLE_CONST_PROPS_END
};
LV_STYLE_CONST_INIT(UiStyle_resourceName, UiStyle_resourceNameProps_);

static lv_style_const_prop_t const UiStyle_resourceSizeProps_[] = {
    UI_TEXT_PROPS_(32, LV_TEXT_ALIGN_RIGHT),
    LV_STYLE_CONST_PROPS_END
};
LV_STYLE_CONST_INIT(UiStyle_resourceSize, UiStyle_resourceSizeProps_);

static lv_style_const_prop_t const UiStyle_resourceUnitProps_[] = {
    UI_TEXT_PROPS_(11, LV_TEXT_ALIGN_RIGHT),
    {.prop = LV_STYLE_ALIGN, .value = {.num = LV_ALIGN_TOP_RIGHT}},
    LV_STYLE_CONST_PROPS_END
};
LV_STYLE_CONST_INIT(UiStyle_resourceUnit, UiStyle_resourceUnitProps_);

static lv_style_const_prop_t const UiStyle_resourcePercentProps_[] = {
    UI_TEXT_PROPS_(36, LV_TEXT_ALIGN_CENTER),
    {.prop = LV_STYLE_X, .value = {.num = 33}},
    {.prop = LV_STYLE_Y, .value = {.num = 21}},
    LV_STYLE_CONST_PROPS_END
};
LV_STYLE_CONST_INIT(UiStyle_resourcePercent, UiStyle_resourcePercentProps_);

static lv_style_const_prop_t const UiStyle_resourceMinimumProps_[] = {
    UI_TEXT_PROPS_(48, LV_TEXT_ALIGN_LEFT),
    {.prop = LV_STYLE_ALIGN, .value = {.num = LV_ALIGN_TOP_MID}},
    {.prop = LV_STYLE_X, .value = {.num = -28}},
    LV_STYLE_CONST_PROPS_END
};
LV_STYLE_CONST_INIT(UiStyle_resourceMinimum, UiStyle_resourceMinimumProps_);

static lv_style_const_prop_t const UiStyle_resourceMaximumProps_[] = {
    UI_TEXT_PROPS_(48, LV_TEXT_ALIGN_RIGHT),
    {.prop = LV_STYLE_ALIGN, .value = {.num = LV_ALIGN_TOP_MID}},
    {.prop = LV_STYLE_X, .value = {.num = 42}},
    LV_STYLE_CONST_PROPS_END
};
LV_STYLE_CONST_INIT(UiStyle_resourceMaximum, UiStyle_resourceMaximumProps_);

static lv_style_const_prop_t const UiStyle_liveLabelProps_[] = {
    UI_TEXT_PROPS_(LV_PCT(100), LV_TEXT_ALIGN_AUTO),
    LV_STYLE_CONST_PROPS_END
};
LV_STYLE_CONST_INIT(UiStyle_liveLabel, UiStyle_liveLabelProps_);

static lv_style_const_prop_t const UiStyle_signalLabelProps_[] = {
    UI_TEXT_PROPS_(LV_SIZE_CONTENT, LV_TEXT_ALIGN_AUTO),
    LV_STYLE_CONST_PROPS_END
};
LV_STYLE_CONST_INIT(UiStyle_signalLabel, UiStyle_signalLabelProps_);

static lv_style_const_prop_t const UiStyle_livePercentProps_[] = {
    UI_TEXT_PROPS_(LV_PCT(100), LV_TEXT_ALIGN_CENTER),
    {.prop = LV_STYLE_ALIGN, .value = {.num = LV_ALIGN_CENTER}},
    LV_STYLE_CONST_PROPS_END
};
LV_STYLE_CONST_INIT(UiStyle_livePercent, UiStyle_livePercentProps_);

#undef UI_TEXT_PROPS_

static lv_style_const_prop_t const UiStyle_threadHeaderProps_[] = {
    {.prop = LV_STYLE_WIDTH, .value = {.num = LV_PCT(100)}},
    {.prop = LV_STYLE_HEIGHT, .value = {.num = 17}},
    LV_STYLE_CONST_PROPS_END
};
LV_STYLE_CONST_INIT(UiStyle_threadHeader, UiStyle_threadHeaderProps_);

static lv_style_const_prop_t const UiStyle_threadResourceRowProps_[] = {
    {.prop = LV_STYLE_WIDTH, .value = {.num = LV_PCT(100)}},
    {.prop = LV_STYLE_HEIGHT, .value = {.num = 23}},
    LV_STYLE_CONST_PROPS_END
};
LV_STYLE_CONST_INIT(UiStyle_threadResourceRow,
                    UiStyle_threadResourceRowProps_);

static lv_style_const_prop_t const UiStyle_resourceHeaderProps_[] = {
    {.prop = LV_STYLE_WIDTH, .value = {.num = LV_PCT(100)}},
    {.prop = LV_STYLE_HEIGHT, .value = {.num = 27}},
    LV_STYLE_CONST_PROPS_END
};
LV_STYLE_CONST_INIT(UiStyle_resourceHeader, UiStyle_resourceHeaderProps_);

static lv_style_const_prop_t const UiStyle_resourceSizeGroupProps_[] = {
    {.prop = LV_STYLE_WIDTH, .value = {.num = 43}},
    {.prop = LV_STYLE_HEIGHT, .value = {.num = 17}},
    {.prop = LV_STYLE_ALIGN, .value = {.num = LV_ALIGN_TOP_RIGHT}},
    {.prop = LV_STYLE_X, .value = {.num = -2}},
    LV_STYLE_CONST_PROPS_END
};
LV_STYLE_CONST_INIT(UiStyle_resourceSizeGroup,
                    UiStyle_resourceSizeGroupProps_);

static lv_style_const_prop_t const UiStyle_resourceGaugeProps_[] = {
    {.prop = LV_STYLE_WIDTH, .value = {.num = 102}},
    {.prop = LV_STYLE_HEIGHT, .value = {.num = 35}},
    LV_STYLE_CONST_PROPS_END
};
LV_STYLE_CONST_INIT(UiStyle_resourceGauge, UiStyle_resourceGaugeProps_);

static lv_style_const_prop_t const UiStyle_resourceRangeProps_[] = {
    {.prop = LV_STYLE_WIDTH, .value = {.num = LV_PCT(100)}},
    {.prop = LV_STYLE_HEIGHT, .value = {.num = 17}},
    LV_STYLE_CONST_PROPS_END
};
LV_STYLE_CONST_INIT(UiStyle_resourceRange, UiStyle_resourceRangeProps_);

static lv_style_const_prop_t const UiStyle_switchTrackProps_[] = {
    {.prop = LV_STYLE_WIDTH, .value = {.num = 37}},
    {.prop = LV_STYLE_HEIGHT, .value = {.num = 18}},
    {.prop = LV_STYLE_RADIUS, .value = {.num = LV_RADIUS_CIRCLE}},
    {.prop = LV_STYLE_BG_OPA, .value = {.num = LV_OPA_COVER}},
    {.prop = LV_STYLE_BG_COLOR,
     .value = {.color = LV_COLOR_MAKE(0x81, 0x94, 0x80)}},
    {.prop = LV_STYLE_ANIM_DURATION, .value = {.num = 0}},
    LV_STYLE_CONST_PROPS_END
};
LV_STYLE_CONST_INIT(UiStyle_switchTrack, UiStyle_switchTrackProps_);

static lv_style_const_prop_t const UiStyle_switchIndicatorProps_[] = {
    {.prop = LV_STYLE_RADIUS, .value = {.num = LV_RADIUS_CIRCLE}},
    {.prop = LV_STYLE_BG_OPA, .value = {.num = LV_OPA_COVER}},
    {.prop = LV_STYLE_BG_COLOR,
     .value = {.color = LV_COLOR_MAKE(0x81, 0x94, 0x80)}},
    LV_STYLE_CONST_PROPS_END
};
LV_STYLE_CONST_INIT(UiStyle_switchIndicator,
                    UiStyle_switchIndicatorProps_);

static lv_style_const_prop_t const UiStyle_switchCheckedProps_[] = {
    {.prop = LV_STYLE_BG_COLOR,
     .value = {.color = LV_COLOR_MAKE(0x00, 0x4E, 0xF6)}},
    LV_STYLE_CONST_PROPS_END
};
LV_STYLE_CONST_INIT(UiStyle_switchChecked, UiStyle_switchCheckedProps_);

static lv_style_const_prop_t const UiStyle_switchKnobProps_[] = {
    {.prop = LV_STYLE_RADIUS, .value = {.num = LV_RADIUS_CIRCLE}},
    {.prop = LV_STYLE_BG_OPA, .value = {.num = LV_OPA_COVER}},
    {.prop = LV_STYLE_BG_COLOR,
     .value = {.color = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF)}},
    {.prop = LV_STYLE_PAD_TOP, .value = {.num = -2}},
    {.prop = LV_STYLE_PAD_BOTTOM, .value = {.num = -2}},
    {.prop = LV_STYLE_PAD_LEFT, .value = {.num = -2}},
    {.prop = LV_STYLE_PAD_RIGHT, .value = {.num = -2}},
    LV_STYLE_CONST_PROPS_END
};
LV_STYLE_CONST_INIT(UiStyle_switchKnob, UiStyle_switchKnobProps_);
