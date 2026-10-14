//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software; it comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
//============================================================================
#include "w_nvm.h"

#include "lvgl.h"
#include "widget_io_priv.h"

//............................................................................
void W_Nvm_init(struct W_Nvm *w) {
    w->label = (struct _lv_obj_t *)0;
}
//............................................................................
void W_Nvm_create(struct W_Nvm *w, struct _lv_obj_t *parent,
                  int32_t x, int32_t y) {
    w->label = WidgetIO_label(parent, "NVM_Blinky -", 0xFFFFFFU);
    lv_obj_set_pos((lv_obj_t *)w->label, (lv_coord_t)x, (lv_coord_t)y);
    lv_obj_set_width((lv_obj_t *)w->label, 160);
    lv_label_set_long_mode((lv_obj_t *)w->label, LV_LABEL_LONG_MODE_CLIP);
}
//............................................................................
void W_Nvm_set(struct W_Nvm *w, uint32_t value) {
    lv_label_set_text_fmt((lv_obj_t *)w->label, "NVM_Blinky %u",
                          (unsigned)value);
}
