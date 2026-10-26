//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef UI_METRICS_H_
#define UI_METRICS_H_

#include <stdbool.h>
#include <stdint.h>

#define UI_METRIC_TASK_ROWS 5U
#define UI_METRIC_ROWS 9U

typedef struct {
    uint32_t free;
    uint32_t total;
    bool available;
} UiResourceLevel;

typedef struct {
    uint32_t revision;
    uint32_t cpuPct;
    uint32_t ramUsed;
    uint32_t ramTotal;
    UiResourceLevel rows[UI_METRIC_ROWS];
} UiMetricsSnapshot;

// Runtime observation component. One UI-thread owner; no LVGL dependency.
// Sampling preserves historical stack/pool/inbox minima supplied by owners.
typedef struct {
    uint32_t idle;
    uint32_t total;
    UiMetricsSnapshot snapshot;
} UiMetrics;

void UiMetrics_init(UiMetrics *me);
void UiMetrics_sample(UiMetrics *me);

#endif // UI_METRICS_H_
