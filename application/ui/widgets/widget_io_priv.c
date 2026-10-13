//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "widget_io_priv.h"

#include "lvgl.h"

//............................................................................
// A label with a colour: the only shape the pages ever ask for.
struct _lv_obj_t *WidgetIO_label(struct _lv_obj_t *parent, char const *text,
                                 uint32_t colour) {
    lv_obj_t *label = lv_label_create((lv_obj_t *)parent);

    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(colour), 0);

    return label;
}
//............................................................................
// Milliseconds with one decimal: the readouts are "12.5 ms", not 12 or 13.
void WidgetIO_setMs(struct _lv_obj_t *label, uint32_t us) {
    lv_label_set_text_fmt((lv_obj_t *)label, "%u.%u ms",
                          (unsigned)(us / 1000U),
                          (unsigned)((us % 1000U) / 100U));
}
//............................................................................
//............................................................................
unsigned WidgetIO_poolUsedPct(void) {
    lv_mem_monitor_t mon;

    lv_mem_monitor(&mon);

    return (unsigned)mon.used_pct;
}
//............................................................................
uint32_t WidgetIO_pct(uint32_t used, uint32_t total) {
    uint32_t const pct = (total == 0U) ? 0U : ((used * 100U) / total);

    return (pct > 100U) ? 100U : pct;
}
