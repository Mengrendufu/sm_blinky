//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef W_CARD_H_
#define W_CARD_H_

#include <stdint.h>

struct _lv_obj_t;

// One card: rounded, half transparent, placed absolutely, with two key and
// value pairs. The values are written through the verbs below, so nobody else
// needs the labels.
struct W_Card {
    struct _lv_obj_t *vA;
    struct _lv_obj_t *vB;
};

void W_Card_create(struct W_Card *card, struct _lv_obj_t *parent,
                   int32_t x, int32_t y, char const *k1, char const *k2);
void W_Card_setA(struct W_Card *card, char const *fmt, ...);
void W_Card_setB(struct W_Card *card, char const *fmt, ...);
void W_Card_setAms(struct W_Card *card, uint32_t us);
void W_Card_setBms(struct W_Card *card, uint32_t us);

#endif // W_CARD_H_
