// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The fixed delay's rule (FixedDelayStep), and the rule run ahead of the ticks (FixedDelayAhead) to
// tell, before a frame is copied, whether any tick will show it.
//
// OBS schedules its ticks on a grid of frame periods, which the plugin's ticks keep to within
// microseconds (findings\pipeline.md). So from the picker's last tick and frame shown, the rule can
// be run over the coming ticks. Whether it shows frame N depends only on the frames up to N+1: it
// moves onto N, or past N only while N+1 is overdue. So once N+1 has arrived, the run ahead decides N.
// It assumes every copy is ready by the time the frame is due. Logic only; nothing allocated after
// construction. Knows nothing about textures or OBS.
#pragma once

#include <cstdint>

// The fixed delay's rule at one tick at `tick`: from the frame shown last (place `shown` in `due`, the
// due times of `count` frames in order), the place of the frame the tick shows. The next one unless it
// is clearly not due yet (4 ms of slack: the clocks slipped, show the current one again), and past it
// only while the one after is clearly overdue.
int FixedDelayStep(int shown, const double* due, int count, double tick);

class FixedDelayAhead {
public:
    // Forgets every frame and the run (the picker restarted).
    void Reset();
    // Each new frame, in arrival order.
    void Add(uint64_t serial, double smoothTime);
    // Runs ahead from the picker's state: it showed `shown` at `tick`, ticks come every `tickPeriod`, and
    // a frame is due `outDelay` after its de-jittered arrival. False if `shown` isn't among the frames.
    bool Start(uint64_t shown, double tick, double tickPeriod, double outDelay);
    bool Started() const { return started_; }
    // Moves the grid onto the picker's tick at `tick`, so it doesn't drift from the ticks over a long
    // run (the period rounded, the clocks' correction).
    void Follow(double tick);
    // Whether a tick will show the newest frame Added, given that the frame after it arrived with the
    // de-jittered arrival `nextSmoothTime`.
    bool WillShowNewest(double nextSmoothTime);

private:
    static constexpr int kFrames = 256;  // the frames from the one shown on: over half a second at 400 Hz

    double Due(int64_t frame) const { return smooth_[frame % kFrames] + outDelay_; }
    double TickAt(int64_t k) const { return start_ + static_cast<double>(k) * tickPeriod_; }
    // The rule at tick `k` from frame `shown` (both absolute), over the frames up to `last` and then a
    // frame due at `afterDue`.
    int64_t Step(int64_t shown, int64_t last, double afterDue, int64_t k);

    uint64_t serial_[kFrames] = {};
    double smooth_[kFrames] = {};
    int64_t count_ = 0;  // frames Added; frame i is at i % kFrames
    bool started_ = false;
    double start_ = 0, tickPeriod_ = 0, outDelay_ = 0;
    int64_t pick_ = 0;              // the frame the run ahead shows at its last tick
    int64_t k_ = 0;                 // ...and that tick
    double due_[kFrames + 1] = {};  // scratch for FixedDelayStep
};
