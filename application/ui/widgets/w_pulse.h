//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef W_PULSE_H_
#define W_PULSE_H_

#include <stdint.h>

struct _lv_obj_t;

// The live page's motion: a bar whose value sweeps to and fro on LVGL's own
// animation. It is a bar on purpose - a bar redraws its indicator and never
// garbles, while an arc (lv_spinner included) redraws a narrow area and tears
// on this build (2026-10-13, both seen on the glass).
struct W_Pulse {
    struct _lv_obj_t *bar;
};

void W_Pulse_init(struct W_Pulse *w);
void W_Pulse_create(struct W_Pulse *w, struct _lv_obj_t *parent,
                    int32_t x, int32_t y, int32_t width);

#endif // W_PULSE_H_
