//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef W_SWITCH_H_
#define W_SWITCH_H_

#include <stdbool.h>
#include <stdint.h>

struct _lv_obj_t;

// A switch the LED model drives: it flips when the AO's own LED does, so the
// motion is the board's, not decoration.
struct W_Switch {
    struct _lv_obj_t *sw;
};

void W_Switch_init(struct W_Switch *w);
void W_Switch_create(struct W_Switch *w, struct _lv_obj_t *parent,
                     int32_t x, int32_t y);
void W_Switch_set(struct W_Switch *w, bool on);

#endif // W_SWITCH_H_
