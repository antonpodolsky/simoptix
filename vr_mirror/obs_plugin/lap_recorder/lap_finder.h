// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// LapFinder: finds the recorded lap in the game's session as the telemetry reads it: one full lap, from a
// crossing of the line to the next, driven without a visit to the pits. It waits until it has seen the car
// out of the pits on a lap, so the crossing that starts the lap is a real one (not the lap counter moving
// at the pit exit), and gives up on a lap the player leaves: the pits, the menus, a pause, a new session.
//
// Logic only: the session comes in as a parameter, and it makes no calls (lap_recorder.h acts on it).
#pragma once

#include "shared/telemetry/telemetry_model.h"

enum class LapStep { kNone, kStart, kEnd, kDiscard };

struct LapDecision {
    LapStep step = LapStep::kNone;
    double at = 0;               // kStart, kEnd: the crossing of the line (performance-counter seconds)
    const char* note = nullptr;  // what happened, for the log
};

class LapFinder {
public:
    // A lap longer than this is given up (s): the game must have stopped the clock.
    static constexpr double kLongestLapS = 900;

    void Reset() { *this = LapFinder(); }
    // The session as read now (Telemetry::SessionAt).
    LapDecision Step(const SessionState& s);
    bool Recording() const { return state_ == State::kRecording; }

private:
    enum class State { kWaitForCar, kWaitForLine, kRecording };

    State state_ = State::kWaitForCar;
    unsigned session_ = 0;  // the driving session the lap belongs to
    int lap_ = -1;          // the lap the car was last seen on, out of the pits
    double start_ = 0;      // kRecording: when the lap started
};
