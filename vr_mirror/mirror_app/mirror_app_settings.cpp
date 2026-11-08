// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "mirror_app_settings.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace {

    std::wstring IniPath() {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring p = path;
        return p.substr(0, p.find_last_of(L"\\/") + 1) + L"simoptix_vr_mirror.ini";
    }

    // Setting keys are plain ASCII; the ini path may not be.
    std::wstring Wide(const char* ascii) {
        return std::wstring(ascii, ascii + strlen(ascii));
    }

    float IniFloat(const char* key, float def) {
        wchar_t buf[64];
        GetPrivateProfileStringW(L"mirror", Wide(key).c_str(), L"", buf, 64, IniPath().c_str());
        return buf[0] ? float(_wtof(buf)) : def;
    }

    // Reads one setting of either type through the float-valued ini helper.
    struct IniLoader {
        void operator()(const char* key, int& v) const { v = int(IniFloat(key, float(v))); }
        void operator()(const char* key, float& v) const { v = IniFloat(key, v); }
    };

}

MirrorAppSettings LoadSettings() {
    MirrorAppSettings s;
    VisitSettings(s.core, IniLoader());
    s.outputWidth = std::max(320, int(IniFloat("output_width", float(s.outputWidth))));
    s.outputHeight = std::max(180, int(IniFloat("output_height", float(s.outputHeight))));
    wchar_t source[32];
    GetPrivateProfileStringW(L"mirror", L"frame_source", L"pimax", source, 32, IniPath().c_str());
    s.frameSource = _wcsicmp(source, L"openxr") == 0 ? MirrorAppFrameSource::kOpenXr
                                                     : MirrorAppFrameSource::kPimax;

    return s;
}

PipelineConfig ConfigOf(const MirrorAppSettings& s, FrameReport report) {
    PipelineConfig c = ConfigFor(s.core, OutputClock::kFrames, report);
    if (s.trace) {
        c.observer = ObserverKind::kStampTrace;
    }
    return c;
}
