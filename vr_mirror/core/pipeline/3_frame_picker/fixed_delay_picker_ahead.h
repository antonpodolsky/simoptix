// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The fixed delay's rule (FixedDelayStep), and the rule run ahead of the ticks (FixedDelayAhead), the
// fixed delay's forecast: it tells, before a frame is copied, whether any tick will show it.
//
// OBS schedules its ticks on a grid of frame periods, which the plugin's ticks keep to within
// microseconds (findings\pipeline.md). So from the picker's last tick and frame shown (its
// TickRecord), the rule can be run over the coming ticks. Whether it shows frame N depends only on the
// frames up to N+1: it moves onto N, or past N only while N+1 is overdue. So once N+1 has arrived, the
// run ahead decides N. When N arrives it can already tell the frames shown even with N+1 on time,
// whose copy then needn't wait. It keeps the frames it was told of from the frame shown on, so it
// needs nothing of the picker but the record. It assumes every copy is ready by the time the frame is
// due.
// Logic only; nothing allocated after construction. Knows nothing about textures, threads or OBS.
#pragma once

#include <cstdint>

#include "pipeline/3_frame_picker/frame_picker.h"

// The fixed delay's rule at one tick at `tick`: from the frame shown last (place `shown` in `due`, the
// due times of `count` frames in order), the place of the frame the tick shows. The next one unless it
// is clearly not due yet (4 ms of slack: the clocks slipped, show the current one again), and past it
// only while the one after is clearly overdue.
int FixedDelayStep(int shown, const double* due, int count, double tick);

// How long after a frame's de-jittered arrival its copy is certainly done: a host that copies only the
// frames shown holds each until the next comes (up to two display periods, 33 ms at 60 Hz), and the
// picker's copy delay is at most 45 ms. The forecast passes over a frame only after that. With Instant
// a frame is due sooner, and a copy running late there made the picker show a frame the forecast had
// passed over (stage_replay, findings\pipeline.md).
constexpr double kFixedDelayCopyDoneS = 0.1;

class FixedDelayAhead : public ShowForecast {
public:
    // Both say yes until the picker's delay is settled and a tick has shown a frame; then the rule run
    // ahead decides.
    bool Arrived(const TickRecord& record, const StoredFrame& frame) override;
    bool WillShow(const TickRecord& record, const StoredFrame& next) override;

private:
    static constexpr int kFrames = 256;  // the frames from the one shown on: over half a second at 400 Hz

    // Forgets every frame when the picker restarted.
    void FollowRestarts(const TickRecord& record);
    // Whether the rule can run ahead from the record (the delay settled, a tick shown, that frame
    // known), following the record's tick.
    bool RunsAhead(const TickRecord& record);
    // Forgets every frame and the run (the picker restarted).
    void Reset();
    // Each new frame, in arrival order.
    void Add(uint64_t serial, double smoothTime);
    // Runs ahead from the picker's state: it showed `shown` at `tick`, ticks come every `tickPeriod`, and
    // a frame is due `outDelay` after its de-jittered arrival. False if `shown` isn't among the frames.
    bool Start(uint64_t shown, double tick, double tickPeriod, double outDelay);
    // Moves the grid onto the picker's tick at `tick`, so it doesn't drift from the ticks over a long
    // run (the period rounded, the clocks' correction). Nothing changes for the tick it is on.
    void Follow(double tick);
    // Whether a tick will show the newest frame Added, given that the frame after it arrived with the
    // de-jittered arrival `nextSmoothTime`. `keep`: that is the next frame's real arrival, and the run
    // ahead keeps the ticks it settled; false for a guess, which leaves the run as it was.
    bool WillShowNewest(double nextSmoothTime, bool keep);

    double Due(int64_t frame) const { return smooth_[frame % kFrames] + outDelay_; }
    double TickAt(int64_t k) const { return start_ + static_cast<double>(k) * tickPeriod_; }
    // The rule at tick `k` from frame `shown` (both absolute), over the frames up to `last` and then a
    // frame due at `afterDue`.
    int64_t Step(int64_t shown, int64_t last, double afterDue, int64_t k);

    uint64_t serial_[kFrames] = {};
    double smooth_[kFrames] = {};
    int64_t count_ = 0;      // frames Added; frame i is at i % kFrames
    uint32_t restarts_ = 0;  // the record's when the frames were last forgotten
    bool started_ = false;
    double start_ = 0, tickPeriod_ = 0, outDelay_ = 0;
    int64_t pick_ = 0;              // the frame the run ahead shows at its last tick
    int64_t k_ = 0;                 // ...and that tick
    double due_[kFrames + 1] = {};  // scratch for FixedDelayStep
};
