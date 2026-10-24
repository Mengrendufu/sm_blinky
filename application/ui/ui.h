//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
#ifndef UI_H_
#define UI_H_

//============================================================================
// The UI thread: a plain FreeRTOS task with an HSM inside, not an SST AO. At
// run time it owns the panel and the SRAM, and it will be the only caller of
// lv_* once the display layer lands.
//
// Called from main() before the scheduler starts: creates the static task and
// binds the inbox to it. Events posted before that simply wait in the inbox.

// The UI task's static stack, in words. The stats page reads its own
// high-water mark against this, so the number lives in exactly one place.
#define UI_STACK_WORDS 2048U

void Ui_start(void);

// Mounted by the application on the BSP tick host; timer daemon context.
// Always posts UI_TIMEOUT_SIG; Ui_start binds the inbox before scheduling.
void Ui_tick(void *context);

#endif // UI_H_
