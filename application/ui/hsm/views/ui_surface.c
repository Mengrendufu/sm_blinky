//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "ui_surface.h"
#include "asset.h"
#include "sm_assert.h"

SM_DEFINE_MODULE("ui_surface")

static void UiSurface_describe_(lv_image_dsc_t * const dsc,
                            void const * const pixels) {
    lv_display_t * const display = lv_display_get_default();
    uint32_t const width =
        (uint32_t)lv_display_get_horizontal_resolution(display);

    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf = LV_COLOR_FORMAT_RGB565;
    dsc->header.w = width;
    dsc->header.h =
        (uint32_t)lv_display_get_vertical_resolution(display);
    dsc->header.stride = width * 2U;
    dsc->data_size = ASSET_SLOT_BYTES;
    dsc->data = (uint8_t const *)pixels;
}


void UiSurface_init(UiSurface *me) {
    lv_display_t * const display = lv_display_get_default();
    *me = (UiSurface){0};
    me->root = lv_obj_create(lv_screen_active());
    lv_obj_set_size(me->root,
        lv_display_get_horizontal_resolution(display),
        lv_display_get_vertical_resolution(display));
    lv_obj_set_pos(me->root, 0, 0);
    lv_obj_set_style_border_width(me->root, 0, 0);
    lv_obj_set_style_radius(me->root, 0, 0);
    lv_obj_set_style_pad_all(me->root, 0, 0);
    lv_obj_set_style_shadow_width(me->root, 0, 0);
    lv_obj_remove_flag(me->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(me->root, lv_color_hex(0x000000U), 0);
    lv_obj_set_style_bg_opa(me->root, LV_OPA_COVER, 0);
}

static void UiSurface_show_(UiSurface *me, lv_image_dsc_t *descriptor,
                             void const *pixels) {
    SM_REQUIRE(lv_obj_get_child_count(me->root) == 0U);
    lv_obj_set_style_bg_image_src(me->root, NULL, 0);
    if (pixels != NULL) {
        UiSurface_describe_(descriptor, pixels);
        lv_obj_set_style_bg_image_src(me->root, descriptor, 0);
    }
}

bool UiSurface_showSplash(UiSurface *me, void const *pixels) {
    UiSurface_show_(me, &me->splash, pixels);
    return pixels != NULL;
}

void UiSurface_showPage(UiSurface *me, void const *pixels) {
    UiSurface_show_(me, &me->background, pixels);
}

struct _lv_obj_t *UiSurface_content(UiSurface *me) {
    return me->root;
}

struct _lv_obj_t *UiSurface_overlay(UiSurface *me) {
    return lv_obj_get_parent(me->root);
}
