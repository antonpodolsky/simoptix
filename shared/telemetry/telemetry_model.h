// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Telemetry model: what the overlays know about the player's car and session, the same for every game.
// Game adapters (shared\telemetry\games\) fill it, converting their own units, axes and codes into these;
// overlays (the G-meter, race auto-record) read only this. Plain data and names: no game, OBS or Win32.
//
// Conventions, for every field:
// - Time: performance-counter seconds (qpc_clock.h), when the adapter read the data.
// - Car axes (ISO 8855), fixed to the car: x forward, y left, z up.
// - SI units: m/s^2, m/s, m. Pedals 0-1.
//
// A field is added only when an overlay uses it, so the model doesn't grow into the union of every
// game's fields.
#pragma once

#include <cstdint>

struct CarVector {
    float x, y, z;  // car axes: x forward, y left, z up
};

// One reading of the player's car.
struct CarSample {
    double t;               // performance-counter seconds when read
    CarVector accel;        // m/s^2, the car's acceleration
    float throttle, brake;  // 0-1, the driver's pedals (unfiltered)
    bool tcActive, absActive;
    unsigned session;  // the driving session it belongs to (SessionState::session)
};

enum class SessionKind : uint8_t { Other, Practice, Qualifying, Race };  // Other: test day, warm-up, ...

// In running order: the phases before the start come first.
enum class SessionPhase : uint8_t {
    BeforeStart,  // before the session, reconnaissance laps, grid walk-through
    Formation,    // the formation lap
    Countdown,    // the start lights are on (on a rolling start: the end of the formation lap)
    Green,
    Caution,  // full-course yellow
    Stopped,
    Over,
    Paused,
    Unknown,
};

// Unknown: not classified
enum class PlayerResult : uint8_t { Unknown, Running, Finished, Retired, Disqualified };

// The session as of the latest update from the game, with or without the player's car.
struct SessionState {
    bool connected;  // the adapter is connected to the game
    double t;        // performance-counter seconds of the latest update (0: none yet)
    SessionKind kind;
    SessionPhase phase;
    PlayerResult result;  // the player's
    bool hasCar;          // the player has a car on track
    bool driving;         // in the car, not at the monitor or in the menus
    float speed;          // m/s, the player's car (0 without one)
    unsigned session;     // counts driving sessions: changes when a new one starts (or one restarts)
};

inline bool Finished(PlayerResult r) {
    return r == PlayerResult::Finished || r == PlayerResult::Retired || r == PlayerResult::Disqualified;
}

// Names for the log.
inline const char* SessionKindName(SessionKind k) {
    switch (k) {
        case SessionKind::Practice:
            return "practice";
        case SessionKind::Qualifying:
            return "qualifying";
        case SessionKind::Race:
            return "race";
        default:
            return "other";
    }
}

inline const char* SessionPhaseName(SessionPhase p) {
    switch (p) {
        case SessionPhase::BeforeStart:
            return "before the start";
        case SessionPhase::Formation:
            return "formation lap";
        case SessionPhase::Countdown:
            return "start-light countdown";
        case SessionPhase::Green:
            return "green flag";
        case SessionPhase::Caution:
            return "full-course yellow";
        case SessionPhase::Stopped:
            return "session stopped";
        case SessionPhase::Over:
            return "session over";
        case SessionPhase::Paused:
            return "paused";
        default:
            return "unknown";
    }
}
