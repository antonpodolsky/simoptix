// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Scores: what a pipeline's output would look like, measured from its numbers alone, for
// stage_replay --pipeline. Per input: black (the output's corners, warped as warp.hlsli does,
// falling outside the shown picture), the view's lag behind the picture it follows, the view's
// jitter while the picture is still and in small moves, how often the smoothing's stop at the edge
// and its planning acted, and frames warped from the wrong pose (against the pose the frame was really drawn
// with: the replays' true lags, the synthetic motions' exact predictions). Knows nothing about D3D.
#pragma once

#include <vector>

#include "replay_poses.h"
#include "runs.h"

namespace stage_replay {

    class Scores {
    public:
        explicit Scores(const Input& in) : in_(in) {}
        void Add(const HostOutput& o);
        // One line: the measures, lower is better in each.
        void Print(const char* name) const;

    private:
        const Input& in_;
        size_t outputs_ = 0, black_ = 0, wrong_ = 0, posed_ = 0;
        unsigned clamps_ = 0, planned_ = 0;
        std::vector<float> lags_;  // deg
        // Per output, for the jitter: its moment (s), the view's direction, the picture's speed (deg/s).
        std::vector<double> times_, speeds_;
        std::vector<Vec3> dirs_;
        Vec3 pictureDir_;
        bool hasPicture_ = false;
    };

}
