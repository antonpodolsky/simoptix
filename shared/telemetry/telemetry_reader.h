// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// TelemetryReader: the one thread in the OBS process that reads the game into the telemetry area
// (telemetry_area.h), for both plugins. It runs in whichever plugin needed telemetry first, and keeps
// running there while anything in either plugin needs it (a G-meter on screen, race auto-record switched
// on); when nothing does, it stops by itself. If its plugin unloads first, it stops, and the other plugin
// starts it again the next time it reads.
//
// While no game is connected it tries each registered game (game_registry.h) about once a second, sleeping
// in between (no busy loop); once one connects, it runs that game's adapter until the game has gone, then
// looks again. It knows no game: only GameAdapter. While connected it logs its own footprint every 10 s
// (CPU, and wake-ups: each update from the game or a wait running out). While no game runs it logs nothing
// past one "waiting" line.
//
// Ensure, Wake and Shutdown: any thread (Telemetry calls them, telemetry.h).
#pragma once

class TelemetryReader {
public:
    // Starts the reader in this plugin if something needs telemetry and no plugin runs it. `wait`: false
    // where the caller must never wait (OBS's graphics thread): if this plugin is busy starting or stopping
    // its own reader, it tries again at the next call.
    static void Ensure(bool wait);
    // Wakes the reader to look again: called when nothing needs telemetry any more, so it stops at once
    // while no game is connected (within the game's wait, 0.2 s, while one is).
    static void Wake();
    // obs_module_unload: stops this plugin's reader, if it runs, waits for it (within 0.2 s), and never
    // starts it again.
    static void Shutdown();
};
