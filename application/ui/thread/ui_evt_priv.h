//============================================================================
// Copyright (C) 2026 Sunny Matato
//
// This program is free software. It comes without any warranty, to
// the extent permitted by applicable law. You can redistribute it
// and/or modify it under the terms of the Do What The Fuck You Want
// To Public License, Version 2, as published by Sam Hocevar.
// See http://www.wtfpl.net/ for more details.
//============================================================================
//============================================================================
//=== Inbox internals -- private to application/ui/thread

#ifndef UI_EVT_PRIV_H_
#define UI_EVT_PRIV_H_

#include <stdbool.h>
#include "FreeRTOS.h"

// How many of the inbox slots are still free, for the stats page's own row.
unsigned Ui_evtInboxFree(void);
unsigned Ui_evtInboxLen(void);
#include "task.h"
#include "sm_ui_evt.h"

// Slot 0 wakes the inbox loop; slot 1 is for operation completion.
#define UI_INBOX_NOTIFY_INDEX 0U

// ui.c binds the thread once it exists; a post from then on also wakes it.
void Ui_evtBind(TaskHandle_t thread);

// UI thread only: one consumer, so the pop side needs no coordination beyond
// the critical section it shares with the producers.
bool Ui_evtPop(Ui_Evt * const out);

#endif // UI_EVT_PRIV_H_
