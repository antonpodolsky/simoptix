// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// HoverHighlight: the frame in which a game's menu lit up an item under the pointer (a hover), near a given
// moment. LMU's menu panel is fixed in the headset's tracking space, so each grey copy is laid onto a grid
// of directions in tracking space (tracking_grid.h) through the head pose its frame was drawn with, and the
// panel stays put however the head moves. A hover lights the item in one frame: a few blocks of the grid
// step in brightness while the rest of the panel holds still. Each frame gets a step: how far its few most
// changed blocks moved from the frame before, while most blocks barely moved. A new page changes most of
// the panel, and gets none. Near a moment, the hover's frame is the one whose step stands out.
//
// Logic only: the frames, their poses and times come in, oldest first. Knows nothing about D3D, OBS, sound
// or threads. One thread at a time.
#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "grey_frame.h"
#include "tracking_grid.h"

// What HoverHighlight::Find saw from `from` to `to`, for the log as well.
struct HoverLook {
    enum class Result {
        kLit,             // a frame's step stands out: `best`
        kNotHeld,         // the frames there aren't all held: `heldFrom` to `heldTo`, if any
        kTooFew,          // fewer than 3 frames there
        kTooSmall,        // the biggest step is under kMinStep (hover_highlight.cpp)
        kNotStandingOut,  // the biggest step is under kStandOut times the median there
    };

    Result result = Result::kNotHeld;
    int frames = 0;               // the frames from `from` to `to`
    double best = 0, second = 0;  // the times of the frames with the biggest and next biggest step
    float bestStep = 0, secondStep = 0, median = 0;  // grey levels of 254
    double heldFrom = 0, heldTo = 0;  // the frames held (performance-counter seconds); 0 if none
    float minStep = 0, standOut = 0;  // what the biggest step had to reach: grey levels, times the median
};

class HoverHighlight {
public:
    explicit HoverHighlight(const GreyLayout& layout);

    // The next frame.
    void Add(const GreyFrame& frame);
    // The frames stop following on: what was held is dropped.
    void Reset();
    // The frame from `from` to `to` (performance-counter seconds) whose step stands out of the others
    // there, and if none does, why not.
    HoverLook Find(double from, double to) const;

private:
    struct Step {
        double time;
        float step;  // grey levels of 254
    };

    GreyLayout layout_;
    std::vector<float> directions_;               // the grid's cells' directions (GridDirections)
    std::vector<uint8_t> grid_;                   // the newest frame on the grid
    std::vector<float> blocks_, previousBlocks_;  // the newest frame's and the one before's block means
    bool hasPrevious_ = false;
    double previousTime_ = 0;
    std::vector<float> changes_;
    std::deque<Step> steps_;  // the last few seconds' frames, oldest first
};
