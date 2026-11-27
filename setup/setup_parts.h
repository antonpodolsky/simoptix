// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The parts SimOptix Setup installs: the two OBS plugins and the OpenXR layer, with the names of the
// files each one is made of and of the resources that carry them inside setup's exe (setup.rc). Each part
// is a DLL and a manifest: OBS's manifest.json for a plugin, the OpenXR loader's for the layer. A plugin
// also has data files beside its manifest: its sources' icons. Plain data. Knows nothing about where the
// parts go or how they are installed.
#pragma once

#include <array>
#include <cstddef>

enum class PartId { kVrMirror, kOverlays, kOpenXrLayer };

// A file in a plugin's data folder, beside its manifest.
struct DataFile {
    const wchar_t* path;      // inside the data folder: L"icons\\gmeter_dark.svg"
    const wchar_t* resource;  // the resource that carries it (setup.rc)
};

inline constexpr DataFile kVrMirrorDataFiles[] = {
    {L"icons\\vr_headset_dark.svg", L"VR_MIRROR_ICON_VR_HEADSET_DARK"},
    {L"icons\\vr_headset_light.svg", L"VR_MIRROR_ICON_VR_HEADSET_LIGHT"},
};

inline constexpr DataFile kOverlaysDataFiles[] = {
    {L"icons\\gmeter_dark.svg", L"OVERLAYS_ICON_GMETER_DARK"},
    {L"icons\\gmeter_light.svg", L"OVERLAYS_ICON_GMETER_LIGHT"},
    {L"icons\\performance_dark.svg", L"OVERLAYS_ICON_PERFORMANCE_DARK"},
    {L"icons\\performance_light.svg", L"OVERLAYS_ICON_PERFORMANCE_LIGHT"},
};

struct Part {
    PartId id;
    const wchar_t* shownName;         // as the window and the messages show it
    const wchar_t* shortName;         // in the run's progress: "Installing Overlays"
    const wchar_t* description;       // what it adds, under the name in the window
    const wchar_t* folderName;        // the plugin's folder in OBS's plugins folder, empty for the layer
    const wchar_t* dllName;           // the file name it is installed under
    const wchar_t* dllResource;       // the resource that carries the DLL (setup.rc)
    const wchar_t* manifestName;      // the manifest's file name
    const wchar_t* manifestResource;  // the resource that carries the manifest
    const DataFile* dataFiles;        // the plugin's data files, nullptr for the layer
    size_t dataFileCount;
};

inline constexpr std::array<Part, 3> kParts = {
    Part{
        PartId::kVrMirror,
        L"VR Mirror OBS plugin",
        L"VR Mirror",
        L"The Pimax Mirror and OpenXR Mirror sources, and race auto-record.",
        L"simoptix_vr_mirror",
        L"simoptix_vr_mirror.dll",
        L"VR_MIRROR_DLL",
        L"manifest.json",
        L"VR_MIRROR_MANIFEST",
        kVrMirrorDataFiles,
        std::size(kVrMirrorDataFiles),
    },
    Part{
        PartId::kOverlays,
        L"Overlays OBS plugin",
        L"Overlays",
        L"The G-Meter Overlay source.",
        L"simoptix_overlays",
        L"simoptix_overlays.dll",
        L"OVERLAYS_DLL",
        L"manifest.json",
        L"OVERLAYS_MANIFEST",
        kOverlaysDataFiles,
        std::size(kOverlaysDataFiles),
    },
    Part{
        PartId::kOpenXrLayer,
        L"OpenXR layer",
        L"OpenXR layer",
        L"Gives OpenXR Mirror the game's own left eye. Loaded into LMU when the game starts.",
        L"",
        L"XR_APILAYER_SIMOPTIX_frame_source.dll",
        L"OPENXR_LAYER_DLL",
        L"XR_APILAYER_SIMOPTIX_frame_source.json",
        L"OPENXR_LAYER_MANIFEST",
        nullptr,
        0,
    },
};

inline const Part& PartOf(PartId id) {
    return kParts[static_cast<size_t>(id)];
}

inline bool IsPlugin(PartId id) {
    return id != PartId::kOpenXrLayer;
}
