// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// TelemetryRing: the game's recent updates (about 10 s at LMU's 100 a second), each with its session
// state and, when it brought one, the player car's sample. One thread writes (the reader, in whichever
// plugin runs it); any thread of either plugin reads, and never waits: each update carries a sequence
// number, written before and after it, so a reader that copied an update while it was being overwritten
// sees that and drops the copy (a seqlock). Plain data and logic: no Win32, OBS or clock. It lives in the
// telemetry area (telemetry_area.h), a block of memory both plugins map, so it holds nothing but plain
// data and lock-free atomics.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "shared/telemetry/telemetry_model.h"

struct TelemetryEntry {
    std::atomic<int64_t> seq;  // update n: 2n+1 while it is written, 2n+2 once done (0: never written)
    double t;                  // performance-counter seconds of the update (session.t)
    SessionState session;
    bool hasCar;
    CarSample car;
};

struct TelemetryRing {
    static constexpr int64_t kSize = 1024;
    std::atomic<int64_t> count;  // updates written so far
    TelemetryEntry entries[kSize];
};

static_assert(std::atomic<int64_t>::is_always_lock_free, "the ring is shared between DLLs: lock-free only");

// The writing thread: one update, its session state and the car's sample (nullptr if it brought none).
void RingWrite(TelemetryRing* r, const SessionState& session, const CarSample* car);

// Copies the car samples of the updates after `since` and up to `upTo` (performance-counter seconds),
// oldest first, into `out` (room for `max`). Returns how many. Reads only the updates it copies, plus one
// to find where to start: nothing allocated, nothing waited for.
size_t RingSince(const TelemetryRing* r, double since, double upTo, CarSample* out, size_t max);

// The session state of the newest update at or before `at` (performance-counter seconds). False if there
// is none: nothing written yet, or every update is newer (`out` then says not connected, t 0).
bool RingSessionAt(const TelemetryRing* r, double at, SessionState* out);
