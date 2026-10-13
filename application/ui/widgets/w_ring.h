//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef W_RING_H_
#define W_RING_H_

#include <stdint.h>

struct _lv_obj_t;

// The 160x160 ring the art leaves room for: the arc, the number inside it and
// the unit under the number. One value drives the arc and the number, so a
// ring can never disagree with what it reads.
struct W_Ring {
    struct _lv_obj_t *arc;
    struct _lv_obj_t *num;
    struct _lv_obj_t *unit;
};

void W_Ring_init(struct W_Ring *ring);
void W_Ring_create(struct W_Ring *ring, struct _lv_obj_t *parent);
void W_Ring_setUnit(struct W_Ring *ring, char const *unit);
void W_Ring_setValue(struct W_Ring *ring, uint16_t value);

#endif // W_RING_H_
