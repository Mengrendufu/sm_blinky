//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef UI_SPLASH_H_
#define UI_SPLASH_H_

#include "widgets/ui_root/ui_root.h"
#include <stdbool.h>

// Stateless Splash orchestration over the persistent content root;
// the root borrows verified RGB565 pixels while they are shown.
// Returns whether pixels were attached; NULL still shows black.
bool UiSplash_show(UiRoot *root, void const *pixels);
void UiSplash_hide(UiRoot *root);

#endif // UI_SPLASH_H_
