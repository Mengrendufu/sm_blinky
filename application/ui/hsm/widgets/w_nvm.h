//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software; it comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
//============================================================================
#ifndef W_NVM_H_
#define W_NVM_H_

#include <stdint.h>
#include <stdbool.h>

struct _lv_obj_t;

// Passive counter label. The HSM supplies the RAM snapshot value;
// this component never accesses persistent storage.
struct W_Nvm {
    struct _lv_obj_t *label;
    uint32_t shown;
    bool valid;
};

// Lifecycle is owned by the HSM; destroy only this component's objects.
void W_Nvm_destroy(struct W_Nvm *me);
void W_Nvm_init(struct W_Nvm *w);
void W_Nvm_create(struct W_Nvm *w, struct _lv_obj_t *parent,
                  int32_t x, int32_t y);
void W_Nvm_set(struct W_Nvm *w, uint32_t value);

#endif /* W_NVM_H_ */
