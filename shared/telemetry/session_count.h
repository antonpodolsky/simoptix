// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// SessionCount: numbers the game's driving sessions, so the overlays can tell when a new one starts (the
// G-meter resets its highest values, race auto-record stops). Logic only: the inputs come from the game's
// adapter (game_adapter.h), the state is plain data. It lives in the telemetry area (telemetry_area.h), not
// in an adapter, so the count carries on when the reader moves from one plugin to the other.
//
// Threads: the reader's thread only.
#pragma once

#include <cstdint>

struct SessionCount {
    unsigned session;  // the current driving session; 0 before the first
    bool known;        // key and clock hold the last physics step's
    uint64_t key;
    double clock;  // s, the game's session clock
};

// A new physics step of the player's car, in the session `key` at the game's session clock `clock` (s).
// Returns the step's session number: a new one when the key changes or the clock goes back by more than
// 0.5 s (a restart).
inline unsigned CountSession(SessionCount* c, uint64_t key, double clock) {
    if (!c->known || key != c->key || clock < c->clock - 0.5) {
        c->session++;
        c->known = true;
        c->key = key;
    }

    c->clock = clock;
    return c->session;
}
