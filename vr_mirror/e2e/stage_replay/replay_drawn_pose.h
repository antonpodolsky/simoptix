// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// ReplayDrawnPose: the drawn pose as the slot check finds it at its best, for stage_replay's
// pipeline runs: each replay frame's pose is Pimax's prediction for the slot it was really rendered
// for (the replay's true lag), the measured head where none is recorded. The slot check and the
// cockpit correction judge real images, which these runs don't have; --stage drawn_pose replays
// them on the replays' thumbnails. Knows nothing about D3D or the fake runtime.
#pragma once

#include <cstdint>
#include <unordered_map>

#include "pipeline/2_drawn_pose/arrival_slot_drawn_pose.h"

namespace stage_replay {

    class ReplayDrawnPose : public ArrivalSlotDrawnPose {
    public:
        // Frame `serial` was rendered for the slot `slots` display periods before its arrival slot.
        void Set(uint64_t serial, int slots) { slots_[serial] = slots; }
        Quat PoseOf(const FrameTimes& f, PoseSource& poses) const override {
            const auto it = slots_.find(f.serial);
            return PredictedPose(f, it == slots_.end() ? 0 : it->second, {}, poses);
        }

    private:
        std::unordered_map<uint64_t, int> slots_;
    };

}
