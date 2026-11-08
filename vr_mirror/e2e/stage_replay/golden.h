// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The golden dump: every output of the pipeline over a fixed matrix of inputs, settings and host
// styles, each float written exactly (hex), so a refactor that must not change behaviour can be
// checked bit for bit (stage_replay --golden; vr_mirror\e2e\README.md, "Stage replay"). Host styles: the
// window (each frame drawn at arrival, or held for the look-ahead delay) and OBS ticks (60 Hz, each
// tick's own moment with the newest due frame standing in as the image). Knows nothing about D3D.
#pragma once

#include <cstdio>
#include <string>

#include "common/settings.h"
#include "replay_poses.h"

namespace stage_replay {

    struct GoldenRun {
        std::string label;
        const Input* input = nullptr;
        MirrorSettings settings;
        bool ticks = false;  // OBS ticks; else the window
        int stabilizeOff = -1,
            stabilizeOn = -1;  // ...where stabilization goes off and on again (with a reset)
    };

    // Runs `run` and appends its outputs to `out`.
    void RunGolden(const GoldenRun& run, FILE* out);

}
