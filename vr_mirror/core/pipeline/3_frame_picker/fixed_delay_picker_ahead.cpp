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

bool FixedDelayAhead::WillShow(const TickRecord& record, const StoredFrame& frame, const StoredFrame& next) {
    if (record.restarts != restarts_) {
        Reset();
        restarts_ = record.restarts;
    }

    Add(frame.serial, frame.smoothTime);

    if (!record.delaySettled || record.tickPeriod <= 0 || record.tick == 0) {
        return true;
    }

    Follow(record.tick);

    if (!started_ && !Start(record.shown, record.tick, record.tickPeriod, record.outDelay)) {
        return true;
    }

    return WillShowNewest(next.smoothTime);
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

bool FixedDelayAhead::WillShowNewest(double nextSmoothTime) {
    const int64_t newest = count_ - 1;

    if (!started_ || newest < pick_) {
        return true;
    }

    // The ticks before the next frame can be due: every frame that matters to them is known, because
    // the frames after the next one are due later still. The run ahead keeps these ticks.
    const double nextDue = nextSmoothTime + outDelay_;
    while (TickAt(k_ + 1) + kSlackS < nextDue) {
        pick_ = Step(pick_, newest, nextDue, k_ + 1);
        k_++;
    }

    // Beyond them, the rule runs on with these frames until it reaches the newest frame or passes it:
    // until then it looks at no frame after the next one.
    int64_t pick = pick_;
    for (int64_t k = k_ + 1; pick < newest && k <= k_ + kMostTicksAhead; k++) {
        pick = Step(pick, newest, nextDue, k);
    }

    return pick == newest;
}

int64_t FixedDelayAhead::Step(int64_t shown, int64_t last, double afterDue, int64_t k) {
    const auto count = static_cast<int>(last - shown + 1);

    for (int i = 0; i < count; i++) {
        due_[i] = Due(shown + i);
    }
    due_[count] = afterDue;

    return shown + FixedDelayStep(0, due_, count + 1, TickAt(k));
}
