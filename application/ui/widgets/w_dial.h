//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software; it comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
//============================================================================
#ifndef W_DIAL_H_
#define W_DIAL_H_

#include <stdint.h>

struct _lv_obj_t;

// The live page's arc: a 240 degree ring whose indicator steps round on its
// own clock. It is an arc on purpose (that is what the page is for), and it
// steps rather than animates on purpose: an arc that redraws every frame is
// drawn through a narrow area and tears on this build (the spinner did,
// 2026-10-13). One step is a full-width band, exactly like the CPU gauge.
struct W_Dial {
    struct _lv_obj_t *arc;
};

void W_Dial_init(struct W_Dial *d);
void W_Dial_create(struct W_Dial *d, struct _lv_obj_t *parent,
                   int32_t x, int32_t y, int32_t size);
void W_Dial_setValue(struct W_Dial *d, unsigned pct);

#endif // W_DIAL_H_
