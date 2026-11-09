// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// SlotCheckDrawnPose: the slot check (setting drawn_pose 1, the default). A frame's drawn
// pose is the runtime's prediction for the display slot it was really rendered for, judged from
// consecutive frames' thumbnails (slot_check_drawn_pose_lag.h has the why and the measures): when
// the GPU falls behind, LMU queues frames, each rendered for its own slot but arriving one or two
// slots later at the usual rhythm, so its arrival slot's prediction would be 11-22 ms too new. Only
// while the frame timing looks disturbed (TimingGate) is a frame judged; otherwise it keeps its
// arrival slot, exactly as ArrivalSlotDrawnPose. A frame's slot is judged again in hindsight as
// later frames are judged (LagFilter), so PoseOf can change until the frame is shown.
//
// Logic only: frames, thumbnails and predicted poses come in as parameters; it knows nothing about
// D3D, PVR or threads. adapters\d3d\drawn_pose_feed feeds it from the GPU; vr_mirror\e2e\stage_replay
// (--stage drawn_pose) from a replay. Its judgements are shared between the feeding thread and
// the output thread under mutex_, held only to copy a few values.
#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

#include "pipeline/2_drawn_pose/arrival_slot_drawn_pose.h"
#include "pipeline/2_drawn_pose/slot_check_drawn_pose_lag.h"

class SlotCheckDrawnPose : public DrawnPose {
public:
    ImageNeeds Needs() const override;
    void Reset() override;
    int Arrived(
        const GameFrame& f,
        const PoseLookup& poses,
        const EyeInfo& eye,
        double period,
        float (*areas)[4]
    ) override;

    void Look(
        const GameFrame& f,
        const std::vector<float>& thumb,
        const std::vector<float>& patches,
        const PoseLookup& poses,
        const EyeInfo& eye,
        double period
    ) override;

    Quat PoseOf(const FrameTimes& f, PoseSource& poses) const override;
    void PosesAt(const double* times, const Quat* heads, int n, PoseSource& poses, Quat* out) const override {
        arrival_.PosesAt(times, heads, n, poses, out);
    }

    void
    PosesBetween(const double* times, const Quat* heads, int n, PoseSource& poses, Quat* out) const override {
        arrival_.PosesBetween(times, heads, n, poses, out);
    }

    // "game behind": the frames judged by image, and those found rendered for an earlier slot.
    void Report(StatsReport& r) override;
    // The slots before its arrival slot that the frame's pose is for, whether that is its own judgement
    // (else the last frame's), and its own judgement before hindsight.
    void Describe(const FrameTimes& f, StatsReport& r) const override;

    // For CockpitDrawnPose, which runs one inside. Whether the timing gate is open after the last
    // Arrived (feeding thread).
    bool GateOpen() const { return gate_.Open(); }
    // Thread-safe like PoseOf:
    // The slots before its arrival slot that frame `serial` was rendered for, in hindsight (on the
    // cheapest path through every frame judged since), if it was judged within the last 2.8 s.
    bool SlotsBeforeOf(uint64_t serial, int* slots) const;
    // ...as judged from the frames up to it.
    bool OwnSlotsBeforeOf(uint64_t serial, int* slots) const;
    // ...for the frame judged last.
    int LastSlotsBefore() const;

private:
    struct Decision {
        uint64_t serial = 0;
        int lag = 0;          // slots before its arrival slot, from the frames up to it
        int64_t update = -1;  // the filter update it was decided by, or follows (-1: none, lag 0)
    };

    // By serial % kDecisions: as long as the filter's hindsight (2.8 s), for frames held that long.
    static constexpr int kDecisions = LagFilter::kHistory;
    // Whether the gate was open at a frame's arrival, by serial % kGates, until its Look.
    struct Gate {
        uint64_t serial = 0;
        bool open = false;
    };

    static constexpr int kGates = 16;

    bool HindsightLocked(uint64_t serial, int* lag) const;

    ArrivalSlotDrawnPose arrival_;
    // Feeding thread only.
    std::vector<float> prev_;  // the previous frame's thumbnail
    GameFrame prevFrame_;
    bool hasPrev_ = false;
    TimingGate gate_;
    Gate gates_[kGates];
    bool runUpdated_ = false;  // the filter's current run has an update
    // Both threads, under mutex_.
    mutable std::mutex mutex_;
    LagFilter filter_;
    int lag_ = 0;  // decided last
    Decision decisions_[kDecisions];
    int statLate_ = 0, statDecided_ = 0;
};
