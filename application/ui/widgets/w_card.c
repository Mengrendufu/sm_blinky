//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "w_card.h"

#include <stdarg.h>

#include "lvgl.h"
#include "widget_io_priv.h"

//............................................................................
static void W_Card_set_(struct _lv_obj_t *label, char const *fmt,
                        va_list ap) {
    char text[24];

    lv_vsnprintf(text, sizeof text, fmt, ap);
    lv_label_set_text((lv_obj_t *)label, text);
}
//............................................................................
void W_Card_create(struct W_Card *card, struct _lv_obj_t *parent,
                   int32_t x, int32_t y, char const *k1, char const *k2) {
    lv_obj_t *box = lv_obj_create((lv_obj_t *)parent);
    lv_obj_t *key;

    lv_obj_set_size(box, 104, 92);
    lv_obj_set_pos(box, (lv_coord_t)x, (lv_coord_t)y);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x0A0E12U), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_80, 0);
    lv_obj_set_style_radius(box, 12, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(0x1E2A32U), 0);
    lv_obj_set_style_shadow_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 8, 0);

    key = WidgetIO_label((struct _lv_obj_t *)box, k1, 0x8E97A0U);
    lv_obj_align((lv_obj_t *)key, LV_ALIGN_TOP_LEFT, 0, 0);
    card->vA = WidgetIO_label((struct _lv_obj_t *)box, "-", 0xFFFFFFU);
    lv_obj_align((lv_obj_t *)card->vA, LV_ALIGN_TOP_LEFT, 0, 14);

    key = WidgetIO_label((struct _lv_obj_t *)box, k2, 0x8E97A0U);
    lv_obj_align((lv_obj_t *)key, LV_ALIGN_TOP_LEFT, 0, 46);
    card->vB = WidgetIO_label((struct _lv_obj_t *)box, "-", 0xFFFFFFU);
    lv_obj_align((lv_obj_t *)card->vB, LV_ALIGN_TOP_LEFT, 0, 60);
}
//............................................................................
void W_Card_setA(struct W_Card *card, char const *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    W_Card_set_(card->vA, fmt, ap);
    va_end(ap);
}
//............................................................................
void W_Card_setB(struct W_Card *card, char const *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    W_Card_set_(card->vB, fmt, ap);
    va_end(ap);
}
//............................................................................
void W_Card_setAms(struct W_Card *card, uint32_t us) {
    WidgetIO_setMs(card->vA, us);
}
//............................................................................
void W_Card_setBms(struct W_Card *card, uint32_t us) {
    WidgetIO_setMs(card->vB, us);
}
