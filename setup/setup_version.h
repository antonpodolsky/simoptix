// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// SimOptix Setup's own version, as shared\build\version.bat wrote it for this build into obj\
// (simoptix_version.h). The plugins inside setup come from the same build, so it is theirs too. Knows
// nothing about where the version is shown.
#pragma once

#include <string>

#include "simoptix_version.h"

// "0.1.0+16.gabc1234". The version is ASCII, so each char widens to itself.
inline std::wstring SetupVersion() {
    const std::string version = SIMOPTIX_VERSION;
    return std::wstring(version.begin(), version.end());
}
