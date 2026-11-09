// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// What SimOptix Setup found on the PC: each part's installed copy against the one inside setup, the
// OpenXR layer's registration, the signature of the layer inside setup, and OBS and LMU. Plain data,
// filled by setup_probe.cpp and read by setup_status.cpp. Knows nothing about how the facts are read.
#pragma once

#include <array>
#include <string>
#include <vector>

// The installed copy of a part against the copy inside setup.
enum class InstalledCopy { kNone, kSame, kDifferent };

// Where OBS looks for a plugin's DLL in C:\ProgramData\obs-studio\plugins\<name>\. OBS 33 moved it from
// bin\64bit\ up into the plugin's folder, flags the old place "Legacy" and drops it from OBS 34.
enum class PluginLayout { kBin64bit, kFlat };

enum class SignatureKind { kNone, kTrusted, kUntrusted };

struct PartFacts {
    InstalledCopy copy = InstalledCopy::kNone;

    // Plugins: a copy of the DLL in the layout this OBS doesn't use, which OBS ignores or flags.
    bool inOtherLayout = false;

    // The layer: its registration with the OpenXR loader, and other registrations of the same layer
    // from other folders.
    bool registered = false;
    bool switchedOn = false;  // registered with the value 0
    std::vector<std::wstring> otherRegistrations;
};

struct SystemFacts {
    bool obsFound = false;
    PluginLayout obsLayout = PluginLayout::kBin64bit;
    bool obsRunning = false;
    bool lmuRunning = false;
    SignatureKind setupLayerSignature = SignatureKind::kNone;  // the layer inside setup
};

struct Facts {
    std::array<PartFacts, 3> parts;  // in the order of kParts
    SystemFacts system;
};
