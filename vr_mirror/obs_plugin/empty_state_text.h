// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// What a VR Mirror source's empty state says: why the view is empty (no game frames), then the one next
// step, which is the game or the recorded lap. Under its rule one thing: the headset's error, else a failed
// lap's, else the recording's steps while a lap is asked for, else the setup's facts (setup_facts.h). Text
// only; knows nothing about OBS, threads or drawing (EmptyState draws it).
#pragma once

#include <string>
#include <vector>

#include "adapters/d3d/frame_source.h"
#include "empty_state.h"
#include "setup_facts.h"

// What the text is made from: the source's steps for an unread headset, the demo's and the lap recorder's
// states, and the setup's facts.
struct EmptyStateTextInput {
    // The steps that get the headset read, before "Close this window and open it again": the source's
    // own, since what to start differs (Pimax Play for Pimax Mirror, any VR software for OpenXR Mirror).
    const std::vector<std::wstring>* headsetSteps = nullptr;
    bool lapWanted = false;    // a lap is asked for (Record a lap)
    bool lapRecorded = false;  // a recorded lap can be played
    bool lapFailed = false;    // the last lap asked for wasn't recorded
    SetupFactsInput setup;
};

// `game`: the game's frame source, which the facts describe.
EmptyText EmptyStateText(const EmptyStateTextInput& in, const FrameSource& game);
