// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// ReportedArrivalTiming: for a frame source that says which display slot each frame was drawn for
// (the OpenXR layer: the display time the game passed with it), and so lays its frames on the display
// grid. Like GridArrivalTiming it answers when the frame arrived without the noise, and the slot it is
// shown in at the rhythm frames arrive in; which pose it was drawn with is the drawn pose's (stage 2,
// from the reported slot, GameFrame::drawnSlot).
//
// The arrival without its noise: measured against the grid the reported slots lie on, the arrival's
// phase within a period is a running circular median over half a second of frames (robust to a late
// one, and to arrivals near the grid's wrap); each arrival keeps its whole periods and loses its
// deviation from that phase. A frame drawn for an earlier slot than usual (the game behind) or seen a
// frame late (its copy checked a frame later) still arrives at the grid's phase, so nothing jumps.
//
// The slot it is shown in: that arrival plus the gap from arrival to drawn-for slot of the frames that
// came on time, the largest such gap over the last two seconds (a late frame's is a period or two
// smaller). On time, a frame is shown in the slot it was drawn for; late, in the slot it arrived for,
// never sharing one with the frame before. A frame without a reported slot keeps its arrival and gets
// no slot.
//
// Logic only: times in, times out; no runtime calls, no locking (one thread per instance: the frame
// source's reader thread).
#pragma once

#include "pipeline/1_arrival_timing/arrival_timing.h"

class ReportedArrivalTiming : public ArrivalTiming {
public:
    void Reset() override { count_ = head_ = gapCount_ = gapHead_ = 0, hasPhase_ = false, lastSlot_ = 0; }
    // Uses the arrival and the reported slot (in.reportedSlot > 0), not the runtime's next slot.
    ArrivalResult Add(const ArrivalInput& in) override;

private:
    static constexpr int kPhases = 45;  // half a second of frames at 90 Hz
    static constexpr int kGaps = 180;   // two seconds
    double phases_[kPhases] = {};       // arrival minus the grid (seconds, within a period)
    int head_ = 0, count_ = 0;
    double phase_ = 0;  // the running median of those, last computed
    bool hasPhase_ = false;
    double gaps_[kGaps] = {};  // drawn-for slot minus the arrival without its noise (seconds)
    int gapHead_ = 0, gapCount_ = 0;
    double lastSlot_ = 0;  // the previous frame's shown slot, 0 none
};
