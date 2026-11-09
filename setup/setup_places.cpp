// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "setup_places.h"

#include "setup_system.h"

std::wstring PluginFolder(const Part& part) {
    return ProgramDataFolder() + L"\\obs-studio\\plugins\\" + part.folderName;
}

std::wstring PluginDllPath(const Part& part, PluginLayout layout) {
    const std::wstring inside = layout == PluginLayout::kFlat ? L"\\" : L"\\bin\\64bit\\";
    return PluginFolder(part) + inside + part.dllName;
}

std::wstring SimOptixFolder() {
    return ProgramFilesFolder() + L"\\simoptix";
}

std::wstring LayerFolder() {
    return SimOptixFolder() + L"\\openxr_frame_source";
}

std::wstring LayerDllPath() {
    return LayerFolder() + L"\\" + PartOf(PartId::kOpenXrLayer).dllName;
}

std::wstring LayerManifestPath() {
    return LayerFolder() + L"\\" + kLayerManifestName;
}

std::wstring InstalledSetupPath() {
    return SimOptixFolder() + L"\\simoptix_setup.exe";
}
