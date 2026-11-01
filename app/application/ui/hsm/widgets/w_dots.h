//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef W_DOTS_H_
#define W_DOTS_H_

struct _lv_obj_t;

// Indicators under the two Sys cards.
struct W_Dots {
    struct _lv_obj_t *dot[6];
};

void W_Dots_init(struct W_Dots *me);
void W_Dots_destroy(struct W_Dots *me);
void W_Dots_create(struct W_Dots *w, struct _lv_obj_t *parent,
                   unsigned active);

#endif // W_DOTS_H_
