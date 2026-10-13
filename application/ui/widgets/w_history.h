//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef W_HISTORY_H_
#define W_HISTORY_H_

#include <stdint.h>

struct _lv_obj_t;

// A sparkline made of bars: one new sample per window, the oldest falling off
// the left. Twelve bars at half a second each is six seconds of history.
#define W_HISTORY_N 12U

struct W_History {
    struct _lv_obj_t *cap;
    struct _lv_obj_t *bar[W_HISTORY_N];
    uint8_t fill[W_HISTORY_N];
};

void W_History_init(struct W_History *w);
void W_History_create(struct W_History *w, struct _lv_obj_t *parent);
void W_History_push(struct W_History *w, unsigned pct);

#endif // W_HISTORY_H_
