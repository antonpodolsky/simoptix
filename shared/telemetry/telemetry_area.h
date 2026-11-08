// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// TelemetryArea: the telemetry both plugins share, in one block of memory per OBS process
// ("Local\simoptix_telemetry_<pid>"): the game's recent updates (telemetry_ring.h), how many
// things need telemetry right now (a G-meter on screen, race auto-record switched on), which plugin runs
// the one reader that fills it (telemetry_reader.h), and the count of driving sessions, which carries on
// when the reader moves from one plugin to the other. The first plugin to ask creates it (zeroed), the
// other opens the same block; a plugin installed alone has it to itself.
//
// Each plugin compiles its own copy of this code, so the block may be read by a build other than the one
// that made it: it starts with a version and its size, and a plugin that finds another version or size
// uses a block of its own instead (it then reads the game itself, as if installed alone) and logs that.
// Bump kVersion whenever this struct, the ring or the telemetry model changes.
//
// Any thread: only atomics are shared between threads; `sessions` belongs to the reader's thread.
#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>

#include "shared/telemetry/session_count.h"
#include "shared/telemetry/telemetry_ring.h"

struct TelemetryArea {
    static constexpr uint32_t kVersion = 2;

    std::atomic<uint32_t> version;  // kVersion once set up
    std::atomic<uint32_t> size;     // sizeof(TelemetryArea) once set up
    std::atomic<int32_t> users;     // things in the process that need telemetry now, in either plugin
    std::atomic<int64_t> reader;    // which plugin's reader runs (its token, telemetry_reader.cpp); 0: none
    // The moment the picture showed at the last read, in either plugin (performance-counter seconds,
    // shown_moment.h; 0: none yet): kept here so it never goes back, whichever plugin reads.
    std::atomic<double> shown;
    SessionCount sessions;  // the reader's thread, whichever plugin it runs in
    TelemetryRing ring;
};

static_assert(std::atomic<double>::is_always_lock_free, "the area is shared between DLLs: lock-free only");

// The process's area. Never null, never freed.
TelemetryArea* TheTelemetryArea();

// Wakes the reader while it waits for a game (auto-reset; one per area). Never null.
HANDLE TelemetryWakeEvent();
