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
    w->caption = NULL;
    w->valid = false;
}
//............................................................................
void W_Nvm_create(struct W_Nvm *w, struct _lv_obj_t *parent,
                  int32_t x, int32_t y) {
    w->valid = false;
    w->caption = WidgetIO_label(parent, "BOOTS", 0xFFFFFFU);
    lv_obj_set_pos(w->caption, x, y);
    lv_obj_set_size(w->caption, 56, 16);
    w->label = WidgetIO_label(parent, "0", 0xFFFFFFU);
    lv_obj_set_pos(w->label, x + 56, y);
    lv_obj_set_size(w->label, 37, 16);
    lv_obj_set_style_text_align(w->label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(w->label, LV_LABEL_LONG_MODE_CLIP);
}
//............................................................................
void W_Nvm_set(struct W_Nvm *w, uint32_t value) {
    if (w->valid && w->shown == value) {
        return;
    }
    w->shown = value;
    w->valid = true;
    lv_label_set_text_fmt((lv_obj_t *)w->label, "%u",
                          (unsigned)value);
}

void W_Nvm_destroy(struct W_Nvm *me) {
    lv_obj_delete((lv_obj_t *)me->label);
    lv_obj_delete(me->caption);
    W_Nvm_init(me);
}
