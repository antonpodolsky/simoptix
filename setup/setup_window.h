// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// SimOptix Setup's window: a row for each part with what is installed and its own buttons (Install,
// Update or Repair, and Uninstall), and buttons for all the parts at once. It reads the PC again whenever
// it is activated, so a closed OBS or LMU shows without a button. The look is setup_look.h's, the
// decisions setup_status.h's, and the work setup_install.h's.
#pragma once

#include <windows.h>

// Shows the window and returns when it is closed, with the process's exit code.
int RunSetupWindow(HINSTANCE instance);
