// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// GameAdapter: what every game's adapter (shared\telemetry\games\<game>\) does for the telemetry reader
// (telemetry_reader.h): connect to its game while it runs, wait for the game's next update, hand it over
// in the telemetry model's terms, and let go when the game has gone. How it gets there (shared memory, a
// network port, an update signal or polling) is its own business; the reader and the overlays can't
// tell games apart.
//
// Adding a game: its folder under shared\telemetry\games\ with an adapter that implements this, and one
// line in game_registry.cpp.
//
// Threads: every call comes from the reader's thread, one adapter at a time. An adapter is made once per
// plugin and never destroyed. It keeps nothing that must survive the reader moving to the other plugin:
// what does (the count of driving sessions) is the reader's, from what the adapter hands over.
#pragma once

#include <cstdint>

#include "shared/telemetry/telemetry_model.h"

// One update from the game.
struct GameUpdate {
    SessionState session;  // all but `connected` and `session`, which the reader fills in
    bool hasCar;           // a new physics step of the player's car: `car` holds it
    CarSample car;         // all but `session`
    // With a car: which driving session the step belongs to, in the game's own terms. A new key (the
    // session type or the track changed, say) or the clock going back (a restart) starts a new session.
    uint64_t sessionKey;
    double sessionClock;  // s, the game's session clock
};

enum class GameRead {
    Update,   // `out` holds a new update
    Nothing,  // no update this time (the wait ran out, or the game was busy)
    Gone,     // the game has gone: the reader calls Close
};

class GameAdapter {
public:
    virtual ~GameAdapter() = default;

    // The game's name, for the log ("Le Mans Ultimate").
    virtual const char* Name() const = 0;
    // Connects, if the game is running; false if it isn't. Called about once a second while no game is
    // connected, so it must be quick and quiet when the game isn't there (no log lines).
    virtual bool Open() = 0;
    // Waits for the game's next update, up to `timeoutMs`. A wait that runs out isn't an end by itself:
    // the adapter decides when silence means the game has quit.
    virtual GameRead Read(int timeoutMs, GameUpdate* out) = 0;
    // Lets go of the game after Open succeeded.
    virtual void Close() = 0;
};
