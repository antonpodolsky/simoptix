// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The icons OBS shows beside a plugin's sources: SVG files in the plugin's data folder, data\icons\, one
// for OBS's dark themes and one for its light themes. OBS 32.2.2 asks a source's get_dark_icon and
// get_light_icon for them when the source's icon type is custom. Included by each plugin's sources, so
// that obs_module_file finds that plugin's own data folder. Knows nothing about which source it is.
#pragma once

#include <obs-module.h>

#include <atomic>
#include <cstdio>

#include "shared/log.h"

// The full path of data\icons\<file>, which OBS frees with bfree. nullptr when the file is missing: OBS
// then shows its own generic source icon. A missing file is logged once for the plugin.
inline const char* FindSourceIcon(const char* file) {
    static std::atomic<bool> logged{false};
    char path[128];
    snprintf(path, sizeof(path), "icons/%s", file);

    char* found = obs_module_file(path);

    if (!found && !logged.exchange(true)) {
        CoreLog("source icon %s not found in the plugin's data folder: OBS shows its own icon", path);
    }

    return found;
}
