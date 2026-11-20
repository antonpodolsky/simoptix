// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The video memory a VR Mirror source's store of copied frames takes, worked out as the source sizes it:
// the frame picker's entries (FixedDelaySlots) from the copy delay it measured, the headset's refresh
// period and OBS's frame period now, and each copy's size (FrameSource::StoredSize) from the game's eye
// image and the canvas now. So it follows OBS's frame rate, the canvas and the settings, with only the
// game's facts measured while it runs. Before the game has run, from what its runtime reported, with the
// longest copy delay. The empty state shows it (setup_facts.h). Logic only;
// knows nothing about OBS or threads.
#pragma once

#include <cstdint>

#include "adapters/d3d/frame_source.h"
#include "common/game_frame.h"
#include "common/settings.h"

// What the empty state's facts need of the game: measured while it runs, and kept until OBS closes or the
// headset reads otherwise (SameHeadset). Before the game has run with the source, what the source's runtime
// reported of the headset (FrameSource::Reported), which is less: never the copy delay.
struct MemoryFacts {
    bool measured = false;  // all of the below measured with the game
    double copyDelay = 0;   // the frame picker's copy delay (s, FramePicker::CopyDelayS); 0: not known
    bool hasEye = false;    // the eye below is known
    EyeInfo eye;
    uint32_t imageWidth = 0, imageHeight = 0;  // the game's own image (pixels); 0: not known
    double period = 0;                         // the headset's refresh period (s); 0: not known
};

// The facts a runtime's report gives; none known if it reported nothing.
MemoryFacts ReportedFacts(const HeadsetReport& report);

// Whether `reading` and `facts` show the headset alike: the same eye image size and refresh rate. A
// resolution or refresh rate changed in the headset's VR software changes one of them.
bool SameHeadset(const MemoryFacts& reading, const MemoryFacts& facts);

// OBS's video now: the canvas (pixels) and the frame period (s; 0: unknown).
struct ObsVideo {
    uint32_t canvasWidth = 0, canvasHeight = 0;
    double framePeriod = 0;
};

// The store for `settings` with `facts` and OBS's video: its entries and the size of each copy (pixels).
// Before the game has run, with the longest copy delay (kMaxCopyDelayS), so the most it can take. False
// while the facts don't tell or OBS's frame rate isn't known.
bool StoreSize(
    const MemoryFacts& facts,
    const MirrorSettings& settings,
    const FrameSource& source,  // the game's
    const ObsVideo& obs,
    int* slots,
    uint32_t* copyWidth,
    uint32_t* copyHeight
);
