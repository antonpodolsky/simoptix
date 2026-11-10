// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Stage 5, warp: the parameters of the per-pixel warp a host draws (warp.hlsli, the GPU half of
// this stage) for one output frame, from the pose the shown image was drawn with, the view to
// show and the output's geometry. Interface and its data; implementations beside it. Knows nothing
// about how the view was chosen, about frames or timing, or about D3D and OBS.
#pragma once

#include "pipeline/pipeline_types.h"
#include "pipeline/stage_stats.h"

// Layout matches the warp shader's parameters (warp.hlsli): seven float4s.
struct WarpParams {
    float m[3][4];     // rows: output camera -> raw eye camera
    float outTan[4];   // x, y: output half-FOV tangents
    float srcTan[4];   // left, right, up, down tangents of the mirror eye
    float rect[4];     // eye's area in the mirror texture (u0, v0, u1, v1)
    float options[4];  // x: 1 = decode one extra sRGB encoding
};

static_assert(sizeof(WarpParams) == 7 * 16, "WarpParams must match warp.hlsli");

class Warp {
public:
    virtual ~Warp() = default;
    // `shown`: the head pose the shown image was drawn with; `view`: the view to show (both
    // head orientations).
    virtual WarpParams Params(const Quat& shown, const Quat& view, const ViewGeometry& g) const = 0;
    // Its numbers since the last Report, for the host's log.
    virtual void Report(StatsReport&) {}
};
