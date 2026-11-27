// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "game_settings.h"

#include <cstdio>
#include <cwchar>

namespace {

    bool ParseScale(const wchar_t* text, double* scale) {
        wchar_t* end = nullptr;
        const double value = wcstod(text, &end);

        if (end == text || *end != L'\0') {
            return false;
        }

        if (value < kMinEyeScale || value > kMaxEyeScale) {
            return false;
        }

        *scale = value;
        return true;
    }

}

bool ParseSettings(int argc, const wchar_t* const* argv, GameSettings* settings, std::string* error) {
    for (int i = 1; i < argc; i++) {
        const wchar_t* name = argv[i];
        const bool hasValue = i + 1 < argc;
        int eye = -1;

        if (wcscmp(name, L"--left-scale") == 0) {
            eye = 0;
        } else if (wcscmp(name, L"--right-scale") == 0) {
            eye = 1;
        } else if (wcscmp(name, L"--loader") == 0 && hasValue) {
            settings->loader = argv[++i];
            continue;
        } else {
            char text[160];
            snprintf(text, sizeof(text), "unknown argument %ls", name);
            *error = text;
            return false;
        }

        if (!hasValue || !ParseScale(argv[i + 1], &settings->eyeScale[eye])) {
            char text[160];

            snprintf(
                text,
                sizeof(text),
                "%ls takes a number from %.1f to %.1f",
                name,
                kMinEyeScale,
                kMaxEyeScale
            );

            *error = text;
            return false;
        }

        i++;
    }

    return true;
}

const char* SettingsUsage() {
    return "usage: simoptix_game.exe [--left-scale <0.1..2>] [--right-scale <0.1..2>] "
           "[--loader <path to openxr_loader.dll>]";
}
