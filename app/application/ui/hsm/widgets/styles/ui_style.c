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
     .value = {.ptr = &lv_font_montserrat_10}},
    {.prop = LV_STYLE_TEXT_COLOR,
     .value = {.color = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF)}},
    LV_STYLE_CONST_PROPS_END
};

LV_STYLE_CONST_INIT(UiStyle_bootCountText,
                    UiStyle_bootCountTextProps_);

static lv_style_const_prop_t const UiStyle_threadCardTextProps_[] = {
    {.prop = LV_STYLE_TEXT_FONT,
     .value = {.ptr = &lv_font_montserrat_14}},
    {.prop = LV_STYLE_TEXT_COLOR,
     .value = {.color = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF)}},
    LV_STYLE_CONST_PROPS_END
};

LV_STYLE_CONST_INIT(UiStyle_threadCardText,
                    UiStyle_threadCardTextProps_);

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

static lv_style_const_prop_t const UiStyle_resourceCardTextProps_[] = {
    {.prop = LV_STYLE_TEXT_FONT,
     .value = {.ptr = &lv_font_montserrat_14}},
    {.prop = LV_STYLE_TEXT_COLOR,
     .value = {.color = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF)}},
    LV_STYLE_CONST_PROPS_END
};

LV_STYLE_CONST_INIT(UiStyle_resourceCardText,
                    UiStyle_resourceCardTextProps_);

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
