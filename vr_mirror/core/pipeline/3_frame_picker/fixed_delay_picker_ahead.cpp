// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "pipeline/3_frame_picker/fixed_delay_picker_ahead.h"

#include <cmath>

namespace {

    constexpr double kSlackS = 0.004;
    constexpr int kMostTicksAhead = 1000;  // a guard: a frame is due within a second or so

}

int FixedDelayStep(int shown, const double* due, int count, double tick) {
    int pick = shown;

    if (pick + 1 < count && due[pick + 1] <= tick + kSlackS) {
        pick++;
    }
    while (pick + 1 < count && due[pick + 1] <= tick - kSlackS) {
        pick++;
    }

    return pick;
}

bool FixedDelayAhead::Arrived(const TickRecord& record, const StoredFrame& frame) {
    FollowRestarts(record);
    Add(frame.serial, frame.smoothTime);

    // As if the next frame came on time, a display period later. A next frame later than that only
    // keeps this one longer; one sooner (rare) means a copy no tick shows, never a frame missing.
    return !RunsAhead(record) || WillShowNewest(frame.smoothTime + record.period, false);
}

bool FixedDelayAhead::WillShow(const TickRecord& record, const StoredFrame& next) {
    FollowRestarts(record);
    return count_ == 0 || !RunsAhead(record) || WillShowNewest(next.smoothTime, true);
}

void FixedDelayAhead::FollowRestarts(const TickRecord& record) {
    if (record.restarts != restarts_) {
        Reset();
        restarts_ = record.restarts;
    }
}

bool FixedDelayAhead::RunsAhead(const TickRecord& record) {
    if (!record.delaySettled || record.tickPeriod <= 0 || record.tick == 0) {
        return false;
    }

    Follow(record.tick);
    return started_ || Start(record.shown, record.tick, record.tickPeriod, record.outDelay);
}

void FixedDelayAhead::Reset() {
    count_ = 0;
    started_ = false;
}

void FixedDelayAhead::Add(uint64_t serial, double smoothTime) {
    serial_[count_ % kFrames] = serial;
    smooth_[count_ % kFrames] = smoothTime;
    count_++;

    if (started_ && count_ - pick_ > kFrames) {
        started_ = false;  // the frame shown is no longer held
    }
}

bool FixedDelayAhead::Start(uint64_t shown, double tick, double tickPeriod, double outDelay) {
    for (int64_t i = count_ - 1; i >= 0 && i >= count_ - kFrames; i--) {
        if (serial_[i % kFrames] != shown) {
            continue;
        }

        pick_ = i;
        k_ = 0;
        start_ = tick;
        tickPeriod_ = tickPeriod;
        outDelay_ = outDelay;
        started_ = true;
        return true;
    }

    return false;
}

void FixedDelayAhead::Follow(double tick) {
    if (!started_) {
        return;
    }

    const auto periods = static_cast<int64_t>(std::llround((tick - start_) / tickPeriod_));
    start_ = tick;
    k_ -= periods;
}

bool FixedDelayAhead::WillShowNewest(double nextSmoothTime, bool keep) {
    const int64_t newest = count_ - 1;

    if (!started_ || newest < pick_) {
        return true;
    }

    // The ticks before the next frame can be due: every frame that matters to them is known, because
    // the frames after the next one are due later still. With the next frame's real arrival (`keep`),
    // the run ahead keeps these ticks.
    const double nextDue = nextSmoothTime + outDelay_;
    int64_t pick = pick_, last = k_;

    while (TickAt(last + 1) + kSlackS < nextDue) {
        pick = Step(pick, newest, nextDue, last + 1);
        last++;
    }

    if (keep) {
        pick_ = pick;
        k_ = last;
    }

    // Beyond them, the rule runs on with these frames until it reaches the newest frame or passes it:
    // until then it looks at no frame after the next one. Passing it relies on the next frame's copy
    // being ready, so a pass sooner than its copy is certainly done counts as a show.
    if (pick == newest) {
        return true;
    }

    for (int64_t k = last + 1; k <= last + kMostTicksAhead; k++) {
        pick = Step(pick, newest, nextDue, k);

        if (pick == newest) {
            return true;
        }

        if (pick > newest) {
            return TickAt(k) < nextSmoothTime + kFixedDelayCopyDoneS;
        }
    }

    return true;
}

int64_t FixedDelayAhead::Step(int64_t shown, int64_t last, double afterDue, int64_t k) {
    const auto count = static_cast<int>(last - shown + 1);

    for (int i = 0; i < count; i++) {
        due_[i] = Due(shown + i);
    }
    due_[count] = afterDue;

    return shown + FixedDelayStep(0, due_, count + 1, TickAt(k));
}
