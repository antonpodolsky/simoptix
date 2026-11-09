// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Installs and removes each part, and keeps SimOptix's entry in Windows' list of installed apps. Needs
// administrator rights (setup.manifest asks for them). Doesn't check whether OBS or LMU run: the window
// decides that first (setup_status.h).
#pragma once

#include <string>

#include "setup_facts.h"
#include "setup_parts.h"

// A plugin goes into the layout of the OBS found (`system`), and its copy in the other layout is
// removed. The layer replaces its files and registration, and other registrations of the same layer.
bool InstallPart(PartId id, const SystemFacts& system, std::wstring* error);

// Removes everything of the part, in either layout and from every registration.
bool RemovePart(PartId id, std::wstring* error);

// With anything installed, copies this setup to C:\Program Files\simoptix and lists SimOptix in Windows'
// installed apps, whose Uninstall runs that copy with /uninstall. With nothing installed, removes both.
bool UpdateAppEntry(bool anythingInstalled, std::wstring* error);
