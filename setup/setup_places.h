// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Where SimOptix Setup puts each part, and the registry keys it writes. The OpenXR layer's folder and
// registration are the same as vr_mirror\frame_sources\openxr\install.ps1's, so either one replaces and
// removes the other's install. Knows nothing about how the parts are installed.
#pragma once

#include <string>

#include "setup_facts.h"
#include "setup_parts.h"

// The OpenXR loader's list of implicit layers. Each value's name is a manifest's full path, and the value
// 0 switches the layer on.
inline constexpr const wchar_t* kLayerRegistryKey = L"SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit";

// The entry in Windows' list of installed apps (Settings, Installed apps).
inline constexpr const wchar_t*
    kAppEntryKey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\SimOptix";

// C:\ProgramData\obs-studio\plugins\<name>
std::wstring PluginFolder(const Part& part);

// The DLL's path in the plugin's folder, in the layout given.
std::wstring PluginDllPath(const Part& part, PluginLayout layout);

// C:\Program Files\simoptix
std::wstring SimOptixFolder();

// C:\Program Files\simoptix\openxr_frame_source
std::wstring LayerFolder();

std::wstring LayerDllPath();
std::wstring LayerManifestPath();

// The copy of setup that Windows' list of installed apps runs to remove SimOptix.
std::wstring InstalledSetupPath();
