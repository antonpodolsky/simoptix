// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lap_finder.h"

namespace {

    // Why the car isn't on a lap that counts, or null if it is.
    const char* OffTrack(const SessionState& s) {
        if (!s.connected) {
            return "the game closed";
        }
        if (!s.hasCar || !s.driving) {
            return "the player left the car";
        }
        if (s.inPits) {
            return "the car is in the pits";
        }
        if (s.phase == SessionPhase::Paused || s.phase == SessionPhase::Stopped
            || s.phase == SessionPhase::Over) {
            return "the session is paused or over";
        }

        return nullptr;
    }

}

LapDecision LapFinder::Step(const SessionState& s) {
    LapDecision d;
    const char* off = OffTrack(s);

    if (state_ == State::kRecording) {
        if (off || s.session != session_) {
            d.step = LapStep::kDiscard;
            d.note = off ? off : "a new session started";
            state_ = State::kWaitForCar;
            lap_ = -1;
            return d;
        }

        if (s.lap == lap_) {
            if (s.t - start_ > kLongestLapS) {
                d.step = LapStep::kDiscard;
                d.note = "the lap took longer than 15 minutes";
                state_ = State::kWaitForCar;
                lap_ = -1;
            }

            return d;
        }

        if (s.lap != lap_ + 1) {
            d.step = LapStep::kDiscard;
            d.note = "the lap counter jumped";
            state_ = State::kWaitForCar;
            lap_ = -1;
            return d;
        }

        d.step = LapStep::kEnd;
        d.at = s.lapStart;
        d.note = "the car crossed the line: the lap is complete";
        state_ = State::kWaitForCar;
        lap_ = -1;
        return d;
    }

    if (off) {
        state_ = State::kWaitForCar;
        lap_ = -1;
        return d;
    }

    if (state_ == State::kWaitForCar || s.session != session_) {
        state_ = State::kWaitForLine;
        session_ = s.session;
        lap_ = s.lap;
        return d;
    }

    if (s.lap == lap_) {
        return d;
    }

    if (s.lap != lap_ + 1) {
        lap_ = s.lap;  // counted anew: wait for the next crossing
        return d;
    }

    d.step = LapStep::kStart;
    d.at = s.lapStart;
    d.note = "the car crossed the line: recording the lap";
    state_ = State::kRecording;
    lap_ = s.lap;
    start_ = s.lapStart;
    return d;
}
