//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef UI_SYS_PAGE_H_
#define UI_SYS_PAGE_H_

#include "widgets/w_gauge.h"
#include "widgets/w_barpanel.h"
#include "widgets/w_dots.h"
#include "metrics/ui_metrics.h"

// Sys presentation subsystem. Borrows the exclusive content root
// and owns the widgets created below it.
typedef struct {
    struct _lv_obj_t *root;
    struct W_Gauge gauge;
    struct W_BarPanel panel;
    struct W_Dots dots;
    bool shown;
    uint32_t revision;
} UiSysPage;

void UiSysPage_init(UiSysPage *me);
void UiSysPage_create(UiSysPage *me, struct _lv_obj_t *parent);
void UiSysPage_refresh(UiSysPage *me, UiMetricsSnapshot const *snapshot);
void UiSysPage_destroy(UiSysPage *me);

#endif // UI_SYS_PAGE_H_
