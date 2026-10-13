//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software; it comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
//============================================================================
#include "w_ram.h"

#include "lvgl.h"
#include "widget_io_priv.h"

//............................................................................
void W_Ram_init(struct W_Ram *r) {
    r->lineA = (struct _lv_obj_t *)0;
    r->lineB = (struct _lv_obj_t *)0;
}
//............................................................................
void W_Ram_create(struct W_Ram *r, struct _lv_obj_t *parent) {
    r->lineA = WidgetIO_label(parent, "-", 0xE0E4E8U);
    lv_obj_align((lv_obj_t *)r->lineA, LV_ALIGN_TOP_MID, 0, 232);

    r->lineB = WidgetIO_label(parent, "-", 0x9AA3ACU);
    lv_obj_align((lv_obj_t *)r->lineB, LV_ALIGN_TOP_MID, 0, 254);
}
//............................................................................
void W_Ram_set(struct W_Ram *r, uint32_t usedBytes, uint32_t totalBytes) {
    uint32_t const usedTenths = (usedBytes * 10U) / 1024U;
    uint32_t const totalTenths = (totalBytes * 10U) / 1024U;
    uint32_t const freeTenths = (totalTenths > usedTenths)
                                ? (totalTenths - usedTenths) : 0U;

    lv_label_set_text_fmt((lv_obj_t *)r->lineA, "RAM %u.%u / %u.%u KB",
                          (unsigned)(usedTenths / 10U),
                          (unsigned)(usedTenths % 10U),
                          (unsigned)(totalTenths / 10U),
                          (unsigned)(totalTenths % 10U));
    lv_label_set_text_fmt((lv_obj_t *)r->lineB, "free %u.%u KB",
                          (unsigned)(freeTenths / 10U),
                          (unsigned)(freeTenths % 10U));
}
