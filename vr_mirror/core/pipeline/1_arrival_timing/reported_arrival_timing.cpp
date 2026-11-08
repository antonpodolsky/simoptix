// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "pipeline/1_arrival_timing/reported_arrival_timing.h"

#include <algorithm>
#include <cmath>

ArrivalResult ReportedArrivalTiming::Add(const ArrivalInput& in) {
    ArrivalResult r;
    r.smooth = in.arrival;
    if (in.reportedSlot <= 0 || in.period <= 0) {
        return r;
    }
    const double p = in.period;

    // Where on the grid the arrival falls, near the running phase (within half a period of it).
    auto near = [p](double x, double around) { return x - p * std::round((x - around) / p); };
    const double phase = near(in.arrival - in.reportedSlot, hasPhase_ ? phase_ : 0);

    phases_[head_] = phase;
    head_ = (head_ + 1) % kPhases;
    count_ = std::min(count_ + 1, kPhases);

    const double around = hasPhase_ ? phase_ : phase;
    double sorted[kPhases];
    for (int i = 0; i < count_; i++) {
        sorted[i] = near(phases_[i], around);
    }
    std::nth_element(sorted, sorted + count_ / 2, sorted + count_);
    phase_ = sorted[count_ / 2];
    hasPhase_ = true;

    r.smooth = in.arrival - (near(phase, phase_) - phase_);  // its whole periods kept, its noise gone

    // The shown slot: the arrival plus the on-time frames' gap to their drawn-for slot (the largest over
    // two seconds: late frames' gaps are a period or more smaller), on the grid.
    gaps_[gapHead_] = in.reportedSlot - r.smooth;
    gapHead_ = (gapHead_ + 1) % kGaps;
    gapCount_ = std::min(gapCount_ + 1, kGaps);
    const double onTime = *std::max_element(gaps_, gaps_ + gapCount_);
    double slot = in.reportedSlot + p * std::round((r.smooth + onTime - in.reportedSlot) / p);
    if (lastSlot_ > 0 && slot < lastSlot_ + 0.5 * p) {
        slot = lastSlot_ + p;  // never the previous frame's
    }
    r.slot = slot;
    r.offset = slot - r.smooth;
    lastSlot_ = slot;
    return r;
}
