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

    for (i = 0U; i < W_BARPANEL_ROWS; ++i) {
        p->unit[i] = units[i];
        // The pitch is the default font's line height plus a gap, and the
        // columns keep a gap of their own: a label box that touches its
        // neighbour gets redrawn through a narrow area when the neighbour
        // moves, and LVGL 9.5 tears the overlapping glyphs (the same narrow
        // redraw the arc suffers from - see w_ring.c). Nothing here may
        // overlap anything else, and the names are kept short for that
        // reason.
        lv_coord_t const y = (lv_coord_t)(142 + (int32_t)i * 18);
        lv_obj_t *bar = lv_bar_create((lv_obj_t *)parent);
        char const * const name = (names[i] != (char const *)0) ? names[i]
                                                               : "-";

        p->name[i] = WidgetIO_label(parent, name, 0xC9D1D9U);
        lv_obj_set_pos((lv_obj_t *)p->name[i], 14, y);
        lv_obj_set_width((lv_obj_t *)p->name[i], 42);
        lv_label_set_long_mode((lv_obj_t *)p->name[i],
                               LV_LABEL_LONG_MODE_CLIP);

        lv_obj_set_size(bar, 70, 10);
        lv_obj_set_pos(bar, 60, (lv_coord_t)(y + 4));
        lv_bar_set_range(bar, 0, 100);
        lv_obj_set_style_radius(bar, 5, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_shadow_width(bar, 0, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x1E2A32U), LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0xC6FF3DU),
                                  LV_PART_INDICATOR);
        p->bar[i] = (struct _lv_obj_t *)bar;

        // 100% is about 35 px in the default font, and a label that is
        // narrower than its text wraps - the percent sign used to fall onto
        // the next row (2026-10-13). So: wide enough for the longest string,
        // and CLIP rather than wrap if anything ever outgrows it.
        p->pct[i] = WidgetIO_label(parent, "0%", 0xFFFFFFU);
        lv_obj_set_pos((lv_obj_t *)p->pct[i], 136, y);
        lv_obj_set_width((lv_obj_t *)p->pct[i], 38);
        lv_obj_set_style_text_align((lv_obj_t *)p->pct[i],
                                    LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_long_mode((lv_obj_t *)p->pct[i],
                               LV_LABEL_LONG_MODE_CLIP);

        p->val[i] = WidgetIO_label(parent, "-", 0x9AA3ACU);
        lv_obj_set_pos((lv_obj_t *)p->val[i], 180, y);
        lv_obj_set_width((lv_obj_t *)p->val[i], 54);
        lv_label_set_long_mode((lv_obj_t *)p->val[i],
                               LV_LABEL_LONG_MODE_CLIP);
    }
}
//............................................................................
// No LV_ANIM_ON here: animating bars would repaint every row each frame
// for the length of every transition, which is the one thing this page cannot
// afford. A direct write redraws only the rows that moved.
void W_BarPanel_setWords(struct W_BarPanel *p, unsigned row, unsigned pct,
                         unsigned words) {
    lv_bar_set_value((lv_obj_t *)p->bar[row], (int32_t)pct, LV_ANIM_OFF);
    lv_label_set_text_fmt((lv_obj_t *)p->pct[row], "%u%%", pct);
    lv_label_set_text_fmt((lv_obj_t *)p->val[row], "%u %s", words,
                          p->unit[row]);
}
//............................................................................
void W_BarPanel_setPair(struct W_BarPanel *p, unsigned row, unsigned pct,
                        unsigned used, unsigned total) {
    lv_bar_set_value((lv_obj_t *)p->bar[row], (int32_t)pct, LV_ANIM_OFF);
    lv_label_set_text_fmt((lv_obj_t *)p->pct[row], "%u%%", pct);
    lv_label_set_text_fmt((lv_obj_t *)p->val[row], "%u/%u", used, total);
}
