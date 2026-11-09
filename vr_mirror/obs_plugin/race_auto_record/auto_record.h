// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// AutoRecord: records races in Le Mans Ultimate by itself. The adapter between RaceRecorder
// (race_recorder.h, which decides) and OBS: while on, it holds the telemetry (shared\telemetry\telemetry.h),
// steps the recorder four times a second from an OBS tick callback (graphics thread; it never waits),
// logs what the recorder says and LMU's session and phase changes, and starts or stops OBS's recording on
// the UI thread. It runs on the view's clock (Telemetry::Shown: now less the delay the VR Mirror view
// publishes), so a race starts and ends in the recording where the recorded video shows it, however far
// the view runs behind the game. Nothing here looks at the video itself.
//
// It is part of VR Mirror because recording is something OBS does as a whole, next to the view,
// not part of an overlay; its switch is in each VR Mirror source's properties (plugin.cpp).
//
// OBS's frontend API (obs-frontend-api.dll, which OBS itself loads) is looked up at run time: the
// plugin links only libobs. Without it, auto-record logs that once and does nothing.
#pragma once

class AutoRecord {
public:
    // Each VR Mirror source with "Record races automatically" ticked calls Enable, and Disable
    // when it's unticked or the source is destroyed: on while at least one has it. UI thread.
    static void Enable();
    static void Disable();
};
