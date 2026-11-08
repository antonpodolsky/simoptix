// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "race_recorder.h"

namespace {

    constexpr float kStill = 1.0f;   // m/s (3.6 km/h): the car stands still
    constexpr double kStillFor = 5;  // s standing still after the finish before the recording stops
    // s away from the race (no car, at the monitor, game closed) before it stops
    constexpr double kAwayFor = 5;
    // s without an update from the game that count as away (LMU's menus send none)
    constexpr double kQuiet = 30;
    constexpr double kStartWait = 10;  // s for OBS to start recording

}

RaceAction RaceRecorder::Step(const RaceInput& in, const char** note) {
    *note = nullptr;
    const SessionState& s = in.session;
    const bool fresh = s.connected && s.t > 0 && in.t - s.t < kQuiet;
    const bool race = s.kind == SessionKind::Race;

    // A new driving session (the next one, or the race restarted) ends the race being recorded. One
    // being started is left to become the recording, and ends by the rules below.
    bool stop = false;
    if (fresh && (!known_ || s.session != session_)) {
        stop = state_ == State::Recording;
        known_ = true;
        session_ = s.session;
        phaseKnown_ = false;
        if (state_ != State::Starting) {
            state_ = State::Waiting;
        }
    }

    const bool prevKnown = phaseKnown_;
    const SessionPhase prevPhase = lastPhase_;
    if (fresh && s.phase != SessionPhase::Paused) {
        lastPhase_ = s.phase, phaseKnown_ = true;
    }
    if (stop) {
        *note = "a new session started: stopping the recording";
        return RaceAction::Stop;
    }

    switch (state_) {
        case State::Waiting:
            return WaitForLights(in, fresh, race, prevKnown && prevPhase <= SessionPhase::Formation, note);
        case State::Starting:
            return CheckStarted(in, note);
        case State::Recording:
            return WatchRace(in, fresh, race, note);
        case State::Done:
            break;
    }

    return RaceAction::None;
}

RaceAction RaceRecorder::WaitForLights(
    const RaceInput& in,
    bool fresh,
    bool race,
    bool afterFormation,
    const char** note
) {
    const SessionState& s = in.session;

    // The start lights: the countdown, or green straight after the formation lap (or before it) if no
    // update caught the countdown.
    const bool lights = fresh && race && s.hasCar && s.result == PlayerResult::Running
                        && (s.phase == SessionPhase::Countdown
                            || ((s.phase == SessionPhase::Green || s.phase == SessionPhase::Caution)
                                && afterFormation));

    if (!lights) {
        return RaceAction::None;
    }

    if (in.recording) {
        state_ = State::Done;
        *note = "the start lights are on, but OBS is already recording: leaving that recording alone";
        return RaceAction::None;
    }

    state_ = State::Starting;
    since_ = in.t;
    *note = "the start lights are on: starting the recording";
    return RaceAction::Start;
}

RaceAction RaceRecorder::CheckStarted(const RaceInput& in, const char** note) {
    if (in.recording) {
        state_ = State::Recording;
        stillSince_ = awaySince_ = -1;
        *note = "recording the race";
    } else if (in.t - since_ > kStartWait) {
        state_ = State::Done;
        *note = "OBS didn't start recording within 10 s: not trying again in this race";
    }

    return RaceAction::None;
}

RaceAction RaceRecorder::WatchRace(const RaceInput& in, bool fresh, bool race, const char** note) {
    const SessionState& s = in.session;

    if (!in.recording) {
        state_ = State::Done;
        *note = "the recording was stopped outside the plugin: leaving it so until the next race";
        return RaceAction::None;
    }

    // The race is over for the car (finished, retired or disqualified) and it stands still.
    const bool still = fresh && s.hasCar && Finished(s.result) && s.speed < kStill;
    stillSince_ = still ? (stillSince_ < 0 ? in.t : stillSince_) : -1;

    if (stillSince_ >= 0 && in.t - stillSince_ >= kStillFor) {
        state_ = State::Done;
        *note = "the car stopped after the finish: stopping the recording";
        return RaceAction::Stop;
    }

    // The player left the race: no car, at the monitor, another session type, or the game quiet or
    // closed.
    const bool away = !fresh || !s.hasCar || !s.driving || !race;
    awaySince_ = away ? (awaySince_ < 0 ? in.t : awaySince_) : -1;

    if (awaySince_ >= 0 && in.t - awaySince_ >= kAwayFor) {
        state_ = State::Done;
        *note = "the player left the race: stopping the recording";
        return RaceAction::Stop;
    }

    return RaceAction::None;
}
