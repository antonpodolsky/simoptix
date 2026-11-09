// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// SimOptix Setup's window: the band with the main action (Install all or Update all), a row per part with
// its own buttons (Install, Update or Repair, and Uninstall), and the footer with Uninstall all and
// Close. A change runs on a thread of its own (setup_run.h), and the window shows its progress until the
// results have stood a moment. It reads the PC again whenever it is activated, so a closed OBS or LMU
// shows without a button. The look is setup_look.h's and setup_buttons.h's, the decisions
// setup_status.h's, and the work setup_install.h's.
#pragma once

#include <windows.h>

// Shows the window and returns when it is closed, with the process's exit code.
int RunSetupWindow(HINSTANCE instance);
