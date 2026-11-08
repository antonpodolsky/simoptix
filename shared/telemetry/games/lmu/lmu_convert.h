// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Le Mans Ultimate's units, axes and codes turned into the telemetry model's (telemetry_model.h). Used
// by the adapter (lmu_telemetry.cpp). Takes LMU's raw numbers, so it needs none of LMU's headers.
// Logic only.
#pragma once

#include "shared/telemetry/telemetry_model.h"

// LMU's car coordinates: +x left, +y up, +z rear (forward is -z).
inline CarVector CarVectorFromLmu(double x, double y, double z) {
    return {float(-z), float(x), float(y)};
}

// mSession: 0 test day, 1-4 practice, 5-8 qualifying, 9 warm-up, 10-13 race.
inline SessionKind SessionKindFromLmu(long session) {
    if (session >= 1 && session <= 4) {
        return SessionKind::Practice;
    }
    if (session >= 5 && session <= 8) {
        return SessionKind::Qualifying;
    }
    if (session >= 10 && session <= 13) {
        return SessionKind::Race;
    }
    return SessionKind::Other;
}

// mGamePhase: 0 before the session, 1 reconnaissance laps, 2 grid walk-through, 3 formation lap,
// 4 start-light countdown (on a rolling start, from when "hold speed" shows), 5 green flag,
// 6 full-course yellow, 7 session stopped, 8 session over, 9 paused.
inline SessionPhase SessionPhaseFromLmu(int phase) {
    switch (phase) {
        case 0:
        case 1:
        case 2:
            return SessionPhase::BeforeStart;
        case 3:
            return SessionPhase::Formation;
        case 4:
            return SessionPhase::Countdown;
        case 5:
            return SessionPhase::Green;
        case 6:
            return SessionPhase::Caution;
        case 7:
            return SessionPhase::Stopped;
        case 8:
            return SessionPhase::Over;
        case 9:
            return SessionPhase::Paused;
        default:
            return SessionPhase::Unknown;
    }
}

// The player's mFinishStatus: 0 none (still running), 1 finished, 2 DNF, 3 DQ. -1: not in the scoring.
inline PlayerResult PlayerResultFromLmu(int status) {
    switch (status) {
        case 0:
            return PlayerResult::Running;
        case 1:
            return PlayerResult::Finished;
        case 2:
            return PlayerResult::Retired;
        case 3:
            return PlayerResult::Disqualified;
        default:
            return PlayerResult::Unknown;
    }
}
