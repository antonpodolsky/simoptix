// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The two questions SimOptix Setup asks, in their words, through setup_dialog.h: before it installs a
// layer that Easy Anti-Cheat would refuse, and before Uninstall all. Each answers true to go on, and its
// default answer is the safe one. Knows nothing about what happens after.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "setup_parts.h"

// `problem` is what is wrong with the layer (UnsignedLayerProblem).
bool AskInstallRefusedLayer(HWND owner, const std::wstring& problem);

bool AskUninstallAll(HWND owner, const std::vector<PartId>& parts);
