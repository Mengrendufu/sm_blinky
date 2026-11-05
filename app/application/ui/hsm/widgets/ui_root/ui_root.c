//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "ui_root.h"
#include "asset.h"

void UiRoot_init(UiRoot *me) {
    lv_display_t * const display = lv_display_get_default();
    lv_obj_t * const screen = lv_screen_active();
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    me->container = lv_obj_create(screen);
    lv_obj_set_size(me->container,
        lv_display_get_horizontal_resolution(display),
        lv_display_get_vertical_resolution(display));
    lv_obj_set_pos(me->container, 0, 0);
    lv_obj_set_style_border_width(me->container, 0, 0);
    lv_obj_set_style_radius(me->container, 0, 0);
    lv_obj_set_style_pad_all(me->container, 0, 0);
    lv_obj_set_style_shadow_width(me->container, 0, 0);
    lv_obj_remove_flag(me->container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(me->container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(me->container, lv_color_hex(0x000000U), 0);
    lv_obj_set_style_bg_opa(me->container, LV_OPA_COVER, 0);
    me->background = (lv_image_dsc_t){0};
}

void UiRoot_setBackground(UiRoot *me, void const *pixels) {
    // Detach before overwriting the descriptor; LVGL may still
    // inspect the old one while invalidating.
    lv_obj_set_style_bg_image_src(me->container, NULL, 0);
    me->background = (lv_image_dsc_t){0};
    if (pixels != NULL) {
        lv_display_t * const display = lv_display_get_default();
        lv_image_dsc_t * const dsc = &me->background;
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
        lv_obj_set_style_bg_image_src(me->container, dsc, 0);
    }
}
