//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef W_BAR_H_
#define W_BAR_H_

struct _lv_obj_t;

// The bar along the bottom: page 1 shows the frame budget in it, page 2 the
// pool. It only ever takes a percentage.
struct W_Bar {
    struct _lv_obj_t *bar;
};

void W_Bar_create(struct W_Bar *w, struct _lv_obj_t *parent);
void W_Bar_setValue(struct W_Bar *w, unsigned pct);

#endif // W_BAR_H_
