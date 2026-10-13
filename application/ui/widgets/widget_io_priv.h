//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef WIDGET_IO_PRIV_H_
#define WIDGET_IO_PRIV_H_

#include <stdint.h>

struct _lv_obj_t;

// What the widget files share between themselves - never a public interface.
struct _lv_obj_t *WidgetIO_label(struct _lv_obj_t *parent, char const *text,
                                 uint32_t colour);
void WidgetIO_setMs(struct _lv_obj_t *label, uint32_t us);
uint32_t WidgetIO_pct(uint32_t used, uint32_t total);
// The LVGL pool's used percentage. It lives here because it is an lv_* call,
// and the state machine that wants the number does not include lvgl.h.
unsigned WidgetIO_poolUsedPct(void);

#endif // WIDGET_IO_PRIV_H_
