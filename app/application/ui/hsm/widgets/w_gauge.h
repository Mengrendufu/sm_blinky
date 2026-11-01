//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef W_GAUGE_H_
#define W_GAUGE_H_

struct _lv_obj_t;

// Pool water level, capacity and block size.
struct W_Gauge {
    struct _lv_obj_t *arc;
    struct _lv_obj_t *num;
    struct _lv_obj_t *unit;
    struct _lv_obj_t *min;
    struct _lv_obj_t *max;
    struct _lv_obj_t *percent;
    struct _lv_obj_t *bytes;
    struct _lv_obj_t *byteUnit;
};

// Lifecycle is owned by the HSM; destroy only this component's objects.
void W_Gauge_destroy(struct W_Gauge *me);
void W_Gauge_init(struct W_Gauge *g);
void W_Gauge_create(struct W_Gauge *g, struct _lv_obj_t *parent);
void W_Gauge_setValue(struct W_Gauge *g, unsigned pct);
void W_Gauge_setBlockBytes(struct W_Gauge *g, unsigned bytes);
void W_Gauge_setMaximum(struct W_Gauge *g, unsigned maximum);

#endif // W_GAUGE_H_
