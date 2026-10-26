//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#include "ui_sys_page.h"
#include "widgets/widget_io_priv.h"
#include "lvgl.h"

static char const * const UiSysPage_names_[UI_METRIC_ROWS] = {
    "ui", "Flsh", "Blky", "tmr", "idle", "nmin", "imin", "smin", "mmin"
};
static char const * const UiSysPage_units_[UI_METRIC_ROWS] = {
    "w", "w", "w", "w", "w", "-", "-", "-", "-"
};

_Static_assert(UI_METRIC_ROWS == W_BARPANEL_ROWS,
               "resource snapshot and panel rows disagree");

void UiSysPage_init(UiSysPage *me) {
    *me = (UiSysPage){0};
    W_Gauge_init(&me->gauge);
    W_BarPanel_init(&me->panel);
}

void UiSysPage_create(UiSysPage *me, struct _lv_obj_t *parent) {
    me->root = parent;
    W_Gauge_create(&me->gauge, me->root);
    W_BarPanel_create(&me->panel, me->root, UiSysPage_names_,
                      UiSysPage_units_);
    W_Dots_create(&me->dots, me->root, 0U);
    me->shown = false;
}

void UiSysPage_refresh(UiSysPage *me, UiMetricsSnapshot const *snapshot) {
    if (me->shown && me->revision == snapshot->revision) {
        return;
    }
    W_Gauge_setValue(&me->gauge, snapshot->cpuPct);
    for (unsigned row = 0U; row < UI_METRIC_ROWS; ++row) {
        UiResourceLevel const * const level = &snapshot->rows[row];
        if (!level->available) {
            continue;
        }
        unsigned const pct = WidgetIO_pct(level->free, level->total);
        if (row < UI_METRIC_TASK_ROWS) {
            W_BarPanel_setWords(&me->panel, row, pct, level->free);
        } else {
            W_BarPanel_setPair(&me->panel, row, pct, level->free,
                               level->total);
        }
    }
    me->revision = snapshot->revision;
    me->shown = true;
}

void UiSysPage_destroy(UiSysPage *me) {
    lv_obj_clean((lv_obj_t *)me->root);
    UiSysPage_init(me);
}
