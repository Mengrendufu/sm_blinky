//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "w_barpanel.h"

#include "lvgl.h"
#include "widget_io_priv.h"

//............................................................................
void W_BarPanel_init(struct W_BarPanel *p) {
    unsigned i;

    p->heading = (struct _lv_obj_t *)0;
    p->count = NULL;

    for (i = 0U; i < W_BARPANEL_ROWS; ++i) {
        p->unit[i] = "-";
        p->name[i] = (struct _lv_obj_t *)0;
        p->bar[i] = (struct _lv_obj_t *)0;
        p->pct[i] = (struct _lv_obj_t *)0;
        p->val[i] = (struct _lv_obj_t *)0;
    }
}
//............................................................................
void W_BarPanel_create(struct W_BarPanel *p, struct _lv_obj_t *parent,
                       char const *const *names,
                       char const *const *units) {
    unsigned i;

    p->heading = WidgetIO_label(parent, "Blinky", 0xFFFFFFU);
    lv_obj_set_pos((lv_obj_t *)p->heading, 14, 71);

    p->count = WidgetIO_label(parent, "0", 0x6FE97BU);
    lv_obj_align(p->count, LV_ALIGN_TOP_RIGHT, -14, 71);
    lv_obj_set_style_text_align(p->count, LV_TEXT_ALIGN_RIGHT, 0);

    for (i = 0U; i < 2U; ++i) {
        p->unit[i] = units[i];
        // The first two rows fit the Figma card.
        lv_coord_t const y = (lv_coord_t)(98 + (int32_t)i * 35);
        lv_obj_t *bar = lv_bar_create((lv_obj_t *)parent);
        char const * const name = (names[i] != (char const *)0) ? names[i]
                                                               : "-";

        p->name[i] = WidgetIO_label(parent, name, 0xF2FAFFU);
        lv_obj_set_pos((lv_obj_t *)p->name[i], 14, y);
        lv_obj_set_size(p->name[i], 38, 16);
        lv_label_set_long_mode((lv_obj_t *)p->name[i],
                               LV_LABEL_LONG_MODE_CLIP);

        lv_obj_remove_style_all(bar);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(bar, 3, LV_PART_INDICATOR);
        lv_obj_set_size(bar, 212, 6);
        lv_obj_set_pos(bar, 14, (lv_coord_t)(y + 21));
        lv_bar_set_range(bar, 0, 100);
        lv_obj_set_style_radius(bar, 3, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_shadow_width(bar, 0, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x1E293BU), LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x6FE97BU),
                                  LV_PART_INDICATOR);
        p->bar[i] = (struct _lv_obj_t *)bar;

        // Keep the percentage object available for the data interface.
        p->pct[i] = WidgetIO_label(parent, "0%", 0xFFFFFFU);
        lv_obj_set_pos((lv_obj_t *)p->pct[i], 150, y);
        lv_obj_set_width((lv_obj_t *)p->pct[i], 38);
        lv_obj_set_style_text_align((lv_obj_t *)p->pct[i],
                                    LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_long_mode((lv_obj_t *)p->pct[i],
                               LV_LABEL_LONG_MODE_CLIP);
        lv_obj_add_flag((lv_obj_t *)p->pct[i], LV_OBJ_FLAG_HIDDEN);

        p->val[i] = WidgetIO_label(parent, "-", 0xF2FAFFU);
        lv_obj_align(p->val[i], LV_ALIGN_TOP_RIGHT, -14, y);
        lv_obj_set_style_text_align((lv_obj_t *)p->val[i],
                                    LV_TEXT_ALIGN_RIGHT, 0);

    }
}
//............................................................................
// No LV_ANIM_ON here: animating bars would repaint every row each frame
// for the length of every transition, which is the one thing this page cannot
// afford. A direct write redraws only the rows that moved.
void W_BarPanel_setCount(struct W_BarPanel *p, uint32_t count) {
    lv_label_set_text_fmt(p->count, "%u", (unsigned)count);
}

void W_BarPanel_setWords(struct W_BarPanel *p, unsigned row, unsigned pct,
                         unsigned words) {
    if (row >= 2U) {
        return;
    }
    lv_bar_set_value((lv_obj_t *)p->bar[row], (int32_t)pct, LV_ANIM_OFF);
    lv_label_set_text_fmt((lv_obj_t *)p->pct[row], "%u%%", pct);
    lv_label_set_text_fmt((lv_obj_t *)p->val[row], "%u %s", words,
                          p->unit[row]);
}
//............................................................................
void W_BarPanel_setPair(struct W_BarPanel *p, unsigned row, unsigned pct,
                        unsigned used, unsigned total) {
    if (row >= 2U) {
        return;
    }
    lv_bar_set_value((lv_obj_t *)p->bar[row], (int32_t)pct, LV_ANIM_OFF);
    lv_label_set_text_fmt((lv_obj_t *)p->pct[row], "%u%%", pct);
    lv_label_set_text_fmt((lv_obj_t *)p->val[row], "%u/%u", used, total);
}

void W_BarPanel_destroy(struct W_BarPanel *me) {
    lv_obj_delete((lv_obj_t *)me->heading);
    lv_obj_delete(me->count);
    for (unsigned i = 0U; i < 2U; ++i) {
        lv_obj_delete((lv_obj_t *)me->name[i]);
        lv_obj_delete((lv_obj_t *)me->bar[i]);
        lv_obj_delete((lv_obj_t *)me->pct[i]);
        lv_obj_delete((lv_obj_t *)me->val[i]);
    }
    W_BarPanel_init(me);
}
