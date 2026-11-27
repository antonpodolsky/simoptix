// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The facts a VR Mirror source's empty state lists under its rule: the headset as the game last showed it
// (before the game has run with the source, as the source's runtime reported it), OBS's canvas and frame
// rate, the video memory the store takes with the settings now, and the highest zoom that still records
// at native resolution. The settings themselves are in the dialog under it. Text
// only; knows nothing about OBS, threads or drawing.
#pragma once

#include <cstdint>
#include <vector>

#include "adapters/d3d/frame_source.h"
#include "common/settings.h"
#include "empty_state.h"
#include "video_memory.h"

// What the facts are made from: the game's facts (MemoryFacts), the source's settings, and OBS's video.
struct SetupFactsInput {
    MemoryFacts facts;
    MirrorSettings settings;
    ObsVideo obs;
    bool headsetUnread = false;  // the source tried to read the headset and couldn't
};

// The facts, in the order shown, each a sentence's start with a capital, sizes and rates alike ("3840 x 2160
// at 90 fps"). The Source row says whether the source's image is the game's full resolution or half, and
// Native recording answers yes (good) or no (a warning), then the zoom or the canvas that keeps it native. A
// row whose facts aren't known is a placeholder: a waiting bar while the headset is being read (Canvas's
// row too, so the values come in together), or that it couldn't be.
std::vector<EmptyFact> SetupFacts(const SetupFactsInput& in, const FrameSource& game);

// The highest zoom, within the Zoom setting's 1 to 3, at which the eye image is at least as wide as the
// canvas shows it, so the recording stays native; 0 if none, below 1.
float MaxNativeZoom(
    const MemoryFacts& facts,
    const MirrorSettings& settings,
    uint32_t canvasWidth,
    uint32_t canvasHeight
);
