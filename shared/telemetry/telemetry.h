// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Telemetry: the game's telemetry as the overlays and race auto-record read it, in either plugin, already
// matched to the view: Shown() is the moment of the game the view shows (VR Mirror's view runs
// behind the game), and every read is as of such a moment, so readers never deal with the delay.
//
// Each thing that needs telemetry (a G-meter on screen, race auto-record switched on) holds it with
// Acquire and Release; while anything in the OBS process does, one reader (telemetry_reader.h) reads
// whichever registered game runs (game_registry.h) into the telemetry area both plugins share
// (telemetry_area.h). Readers name no game.
//
// Acquire and Release: OBS's graphics or UI thread. Shutdown: obs_module_unload. The rest: any thread,
// and never waits.
#pragma once

#include <cstddef>

#include "shared/telemetry/telemetry_model.h"
#include "shared/telemetry/telemetry_ring.h"

class Telemetry {
public:
    static constexpr size_t kRing = size_t(TelemetryRing::kSize);

    // Acquire starts the reader if no plugin runs it; after the last Release in either plugin it stops by
    // itself. Neither waits for the reader's thread.
    static void Acquire();
    static void Release();
    // This plugin is unloading: stops its reader if it runs (the other plugin then takes over).
    static void Shutdown();

    // The moment of the game the view shows, in performance-counter seconds: now, less the view's
    // delay (0 without VR Mirror's view on screen). Never goes back (shown_moment.h). Read it once per
    // frame or step and pass it to the reads below, so they all describe the same moment.
    static double Shown();

    // The car's samples after `since` and up to `at` (performance-counter seconds), oldest first, into `out`
    // (room for `max`; kRing holds them all). Returns how many.
    static size_t Since(double since, double at, CarSample* out, size_t max);
    // The session as of `at`: the newest update at or before it. `connected` false, and the rest zero, if
    // there is none (no game read yet).
    static SessionState SessionAt(double at);
};
