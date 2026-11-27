// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The demo game's settings, read from its command line: how large each eye is drawn, and which OpenXR
// loader to use. Plain logic: it knows nothing of OpenXR or D3D.
#pragma once

#include <string>

struct GameSettings {
    // Each eye's image is the size the headset recommends times its scale, in width and in height.
    double eyeScale[2] = {1.0, 1.0};  // left, right
    std::wstring loader;              // empty: the first loader found in the usual places
};

constexpr double kMinEyeScale = 0.1, kMaxEyeScale = 2.0;

// False, with the reason in `error`, when an argument is unknown or out of range.
bool ParseSettings(int argc, const wchar_t* const* argv, GameSettings* settings, std::string* error);

// The usage line printed with an error.
const char* SettingsUsage();
