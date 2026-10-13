//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef W_TOPBAR_H_
#define W_TOPBAR_H_

#include <stdbool.h>

struct _lv_obj_t;

// The top bar every page shares: the title on the left, and the CPU load with
// the heartbeat led on the right. The page only supplies the title.
struct W_Topbar {
    struct _lv_obj_t *title;
    struct _lv_obj_t *cpu;
    struct _lv_obj_t *led;
};

void W_Topbar_init(struct W_Topbar *bar);
void W_Topbar_create(struct W_Topbar *bar, struct _lv_obj_t *parent,
                     char const *title);
void W_Topbar_setTitle(struct W_Topbar *bar, char const *title);
void W_Topbar_setCpu(struct W_Topbar *bar, unsigned pct);
void W_Topbar_setLed(struct W_Topbar *bar, bool on);

#endif // W_TOPBAR_H_
