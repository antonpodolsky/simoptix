// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "shared/telemetry/telemetry_area.h"

#include <cwchar>

#include "shared/log.h"

namespace {

    struct Opened {
        TelemetryArea* area;
        HANDLE wake;
    };

    // Takes the area if it is new (all zero) or set up by this version; false if another version set it up.
    bool Matches(TelemetryArea* a) {
        uint32_t version = 0, size = 0;
        const bool versionOk = a->version.compare_exchange_strong(version, TelemetryArea::kVersion)
                               || version == TelemetryArea::kVersion;

        const bool sizeOk = a->size.compare_exchange_strong(size, uint32_t(sizeof(TelemetryArea)))
                            || size == uint32_t(sizeof(TelemetryArea));

        return versionOk && sizeOk;
    }

    Opened Open() {
        const DWORD pid = GetCurrentProcessId();
        wchar_t name[80];
        swprintf(name, 80, L"Local\\simoptix_telemetry_%lu", pid);

        // The handle is kept open for the life of the process, like the view.
        HANDLE map = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            0,
            DWORD(sizeof(TelemetryArea)),
            name
        );

        // A smaller block (made by another build) can't be mapped at this size: that fails here.
        void* view = map ? MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(TelemetryArea)) : nullptr;
        TelemetryArea* area = static_cast<TelemetryArea*>(view);
        const bool matches = area && Matches(area);
        if (matches) {
            swprintf(name, 80, L"Local\\simoptix_telemetry_wake_%lu", pid);
            if (HANDLE wake = CreateEventW(nullptr, FALSE, FALSE, name)) {
                return {area, wake};
            }
        }

        if (area && !matches) {
            CoreLog(
                "telemetry: the other plugin is a different build; reading the game separately (install both "
                "from one build to share one reader)"
            );
        } else {
            CoreLog(
                "telemetry: couldn't open the shared telemetry (error %lu); reading the game separately",
                GetLastError()
            );
        }

        // A block of this plugin's own, and an event of its own to go with it: the other plugin's reader must
        // never be woken through it. Both live as long as the process, like the reader that uses them.
        return {new TelemetryArea(), CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    }

    const Opened& TheOpened() {
        static const Opened opened = Open();
        return opened;
    }

}

TelemetryArea* TheTelemetryArea() {
    return TheOpened().area;
}

HANDLE TelemetryWakeEvent() {
    return TheOpened().wake;
}
