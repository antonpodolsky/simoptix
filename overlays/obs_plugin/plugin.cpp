// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The Overlays plugin's OBS module: registers the overlays (the G-meter, ..\gmeter\gmeter_source.cpp, and
// the performance overlay, ..\performance\performance_source.cpp), sends core logging to OBS's log, and
// stops the telemetry reader when it unloads. Nothing else lives here. The performance overlay reads the
// game's frame times from the OpenXR layer's memory (shared\performance\).
//
// It knows nothing of VR Mirror: the two plugins are separate DLLs that never load or call each
// other. What they share passes through memory each opens on its own: the view's delay
// (shared\program_delay.h; 0 with VR Mirror not installed or its source hidden) and the game's
// telemetry, read once for both (shared\telemetry\telemetry.h).

#include <obs-module.h>

#include "shared/crash_dump.h"
#include "shared/guarded.h"
#include "shared/log.h"
#include "shared/telemetry/telemetry.h"
#include "simoptix_version.h"  // written into obj\ by shared\build\version.bat

OBS_DECLARE_MODULE()

MODULE_EXPORT const char* obs_module_name(void) {
    return "SimOptix: Overlays";
}

MODULE_EXPORT const char* obs_module_description(void) {
    return "SimOptix: Overlays: game telemetry and performance drawn over the video: the G-meter and the "
           "performance overlay.";
}

namespace {
    void LogToObs(const char* message) {
        blog(LOG_INFO, "[simoptix_overlays] %s", message);
    }
}

void RegisterGMeterSource();       // ..\gmeter\gmeter_source.cpp
void RegisterPerformanceSource();  // ..\performance\performance_source.cpp

bool obs_module_load(void) {
    return Guarded("load", [] {
        const int dumpsDeleted = InstallCrashDump(L"overlays");
        SetCoreLog(LogToObs);

        if (dumpsDeleted > 0) {
            blog(LOG_INFO, "[simoptix_overlays] %d older crash dump(s) deleted", dumpsDeleted);
        }

        RegisterGMeterSource();
        RegisterPerformanceSource();
        blog(LOG_INFO, "[simoptix_overlays] loaded, version %s", SIMOPTIX_VERSION);
        return true;
    });
}

// The telemetry reader may run in this plugin for VR Mirror too: stop it before the DLL goes, so
// VR Mirror takes it over.
void obs_module_unload(void) {
    Guarded("unload", [] { Telemetry::Shutdown(); });
}
