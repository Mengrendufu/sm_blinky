//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef W_BARPANEL_H_
#define W_BARPANEL_H_

#include <stdint.h>

struct _lv_obj_t;

// A fixed grid of "one line per resource": name, bar, percentage, raw number.
// The rows are made once and only ever written, so a refresh costs the rows
// whose value actually moved.
#define W_BARPANEL_ROWS 8U

struct W_BarPanel {
    char const *unit[W_BARPANEL_ROWS];   // how the raw number is spelled
    struct _lv_obj_t *name[W_BARPANEL_ROWS];
    struct _lv_obj_t *bar[W_BARPANEL_ROWS];
    struct _lv_obj_t *pct[W_BARPANEL_ROWS];
    struct _lv_obj_t *val[W_BARPANEL_ROWS];
};

void W_BarPanel_init(struct W_BarPanel *p);
void W_BarPanel_create(struct W_BarPanel *p, struct _lv_obj_t *parent,
                       char const *const *names,
                       char const *const *units);
void W_BarPanel_setWords(struct W_BarPanel *p, unsigned row, unsigned pct,
                         unsigned words);
void W_BarPanel_setPair(struct W_BarPanel *p, unsigned row, unsigned pct,
                        unsigned used, unsigned total);

#endif // W_BARPANEL_H_
