// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// GridArrivalTiming: de-jitters game frame arrivals against the headset's display grid. Frames
// arrive at a steady offset from the grid, give or take about a millisecond of polling and GPU
// timing noise: the typical offset is learnt (a running median over half a second of arrivals,
// robust to odd readings), arrival + offset is snapped to the nearest grid slot, and the offset
// subtracted again. On average that is the arrival itself, minus the noise; the snapped slot is the
// display slot the frame was rendered for while the game keeps up.
//
// A frame landing well before its nearest slot, with the slot before that one still free and the
// frame before it on time, is a single frame running late for the free slot, not one coming early: it
// gets the free slot (kLateBefore, kOnTime).
//
// Logic only: source times in, times out; no runtime calls, no locking (one thread per instance:
// PimaxSource's poll thread).
#pragma once

#include "pipeline/1_arrival_timing/arrival_timing.h"

class GridArrivalTiming : public ArrivalTiming {
public:
    void Reset() override { count_ = head_ = 0, lastSlot_ = lastPosition_ = 0; }
    // Uses the arrival and the runtime's next slot (in.nextSlot > 0), not a reported slot.
    ArrivalResult Add(const ArrivalInput& in) override;

private:
    static constexpr int kOffsets = 45;  // half a second of arrivals
    // Display periods: a frame whose arrival + offset falls more than kLateBefore before its nearest
    // slot gets the slot before when the previous frame left that one free and fell within kOnTime of
    // its own slot. Measured on 4.4 h of traced driving: rounding to the nearest slot gave
    // two consecutive frames the same slot 237 times, most of them after a late frame that fell 0.3 to
    // 0.5 periods before its slot; the video showed that frame lined up from the next slot's head pose
    // (a one-frame jolt, 3.5 px at 7:39 of race F). Early arrivals fade out by 0.2 periods
    // (0.1-0.2 early: 15,500 frames; 0.2-0.3: 1,000). Without kOnTime the rule also moved frames inside
    // queues, where the drawn pose stage's lag (counted from this slot) then came out a slot off:
    // e2e replay_queued_late_frames_lookahead 7.0-7.3% from the wrong pose against 4.0-5.7%.
    static constexpr double kLateBefore = 0.2;
    static constexpr double kOnTime = 0.15;
    double offsets_[kOffsets] = {};
    int head_ = 0, count_ = 0;
    double lastSlot_ = 0;      // the previous frame's slot (source seconds), 0 none
    double lastPosition_ = 0;  // where the previous frame fell from its slot (display periods)
};
