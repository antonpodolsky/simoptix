// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// RaceRecorder: decides when to start and stop recording a race, from the session state (the telemetry
// model's SessionState, telemetry_model.h) and whether OBS is recording. Logic only: no OBS, Win32 or
// shared-memory calls, and time comes in as a parameter, so it can be driven by anything that has
// the inputs. auto_record.cpp is its adapter to OBS.
//
// - Starts when a race (not practice, qualifying or warm-up) reaches the start lights: the countdown
//   phase (in LMU on a rolling start, when "hold speed" shows), or green straight after the formation
//   lap if no update caught the countdown. Never mid-race: joining a race already under way doesn't
//   start a recording.
// - Stops when the race is over for the player (finished, retired or disqualified) and the car has
//   stood still for 5 s; or when the player has left it (no car, at the monitor, the game quiet or
//   closed), or a new session starts (the next session, or the race restarted).
// - Only stops recordings it started. A recording already running when the lights come on is the
//   user's and is left alone; if the user stops the plugin's recording, it isn't started again in
//   that race.
#pragma once

#include "shared/telemetry/telemetry_model.h"

struct RaceInput {
    double t;              // performance-counter seconds, now
    SessionState session;  // the latest from the game
    bool recording;        // OBS is recording
};

enum class RaceAction { None, Start, Stop };

class RaceRecorder {
public:
    // One step, a few times a second. When the state changed, `note` gets a line for the log (else
    // nullptr).
    RaceAction Step(const RaceInput& in, const char** note);
    // Forgets everything: the next race is decided afresh (used when turned on).
    void Reset() { *this = RaceRecorder(); }

private:
    RaceAction
    WaitForLights(const RaceInput& in, bool fresh, bool race, bool afterFormation, const char** note);
    RaceAction CheckStarted(const RaceInput& in, const char** note);
    RaceAction WatchRace(const RaceInput& in, bool fresh, bool race, const char** note);

    enum class State { Waiting, Starting, Recording, Done };
    State state_ = State::Waiting;
    unsigned session_ = 0;
    bool known_ = false;                              // session_ has been seen
    bool phaseKnown_ = false;                         // lastPhase_ has been seen, this session
    SessionPhase lastPhase_ = SessionPhase::Unknown;  // the phase at the last step with fresh data
    double since_ = 0;                                // Starting: when the start was asked for
    double stillSince_ = -1;  // Recording: since when the car has stood still after the finish (-1: not)
    double awaySince_ = -1;   // Recording: since when the player has been away from the race (-1: not)
};
