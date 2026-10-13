//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef W_SPINNER_H_
#define W_SPINNER_H_

#include <stdint.h>

struct _lv_obj_t;

// The one thing on the glass that moves every frame: a small spinner. 36x36
// is about 0.9 ms a frame, so 30 fps costs roughly 2 percent of the thread.
struct W_Spinner {
    struct _lv_obj_t *spin;
};

void W_Spinner_init(struct W_Spinner *w);
void W_Spinner_create(struct W_Spinner *w, struct _lv_obj_t *parent,
                      int32_t x, int32_t y, int32_t size);

#endif // W_SPINNER_H_
