// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The video memory line of a VR Mirror source's properties: how much its store of copied frames takes
// with the settings in the dialog, from the frame picker's own sizing (FramePicker::SlotsFor) and the
// frame source's copy size (FrameSource::CopySize). When a fact it needs isn't known yet, it says so
// rather than guess. Logic and text only; knows nothing about OBS, threads or which picker runs.
#pragma once

#include <cstdint>
#include <string>

#include "adapters/d3d/frame_source.h"
#include "common/game_frame.h"
#include "common/settings.h"

// What the line needs of the running source, as of its last tick.
struct MemoryFacts {
    bool running = false;  // the source is shown and its frame source runs
    // The store's entries once settled, by SmoothingKind (Instant, Look-ahead); 0 while the frame
    // picker is still measuring.
    int slots[2] = {};
    EyeInfo eye;
    uint32_t canvasWidth = 0, canvasHeight = 0;
};

// "Video memory: 3.1 GB (37 copies of 5084 x 4168)", or what is still missing.
std::string VideoMemoryText(const MemoryFacts& facts, const MirrorSettings& settings, FrameSource& source);
