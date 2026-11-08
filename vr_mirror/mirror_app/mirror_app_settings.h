// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// MirrorAppSettings: the mirror app's settings, read from simoptix_vr_mirror.ini next to the exe: the
// pipeline's (walked with VisitSettings, keys as in vr_mirror\core\common\settings.h), the frame
// source and the window's size. Never written: the app has no controls. Knows nothing about drawing or
// the frame sources themselves.
#pragma once

#include "common/settings.h"
#include "pipeline/pipeline_config.h"

// Where the frames come from: Pimax's client mirror, or the OpenXR layer in the game.
enum class MirrorAppFrameSource { kPimax, kOpenXr };

struct MirrorAppSettings {
    MirrorSettings core;
    MirrorAppFrameSource frameSource = MirrorAppFrameSource::kPimax;  // frame_source: pimax, openxr
    int outputWidth = 2560;  // window client size at startup; match the OBS canvas
    int outputHeight = 1440;
    // SIMOPTIX_VR_MIRROR_TRACE is set (main): the pipeline's observer writes the frame trace
    bool trace = false;
};

MirrorAppSettings LoadSettings();
// The pipeline's configuration for these settings: ConfigFor for a window drawn once per game frame,
// from a frame source that tells `report` of each frame.
PipelineConfig ConfigOf(const MirrorAppSettings& s, FrameReport report);
