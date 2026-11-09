// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Removing all of SimOptix when Windows' list of installed apps runs setup with /uninstall. The copy of
// setup in C:\Program Files\simoptix can't delete itself, so it starts a copy of itself from the temporary
// folder, which waits for it to end and then removes everything.
#pragma once

#include <windows.h>

// `waitFor` is the process id of the installed copy that started this one, or 0.
int RunUninstall(DWORD waitFor);
