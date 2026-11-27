// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// SyncMeasure: auto sync's measure, the part of it that can be done more than one way. It answers which
// audio source has the game's sound and how much that source has to move to match the game's image
// (AutoSyncValues). How it finds out is its own business: HoverSyncMeasure (hover_sync_measure.h) measures
// at the hovers in LMU's menus. AutoSync (auto_sync.h) runs it while a VR Mirror source is shown, applies
// what it answers, and keeps it for the next OBS session.
//
// A measure is made from the values kept from before and what it can look at (SyncInputs), then stepped
// every few hundred milliseconds. Interface only; implementations beside it. One thread.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "auto_sync_values.h"
#include "grey_frame.h"

// What a measure can look at, on the performance counter's clock (seconds). AutoSync provides it.
class SyncInputs {
public:
    virtual ~SyncInputs() = default;

    // Whether the player is driving at `at` (in the car; not in the menus or at the monitor).
    virtual bool Driving(double at) = 0;

    // The grey copies of the frames that arrived from `from` to `to`, oldest first, and where they
    // come from. False while the frames' clock isn't known yet.
    virtual bool Frames(double from, double to, std::vector<GreyFrame>* out) = 0;
    virtual GreyLayout FrameLayout() const = 0;

    // The audio sources there were when the measuring started: how many, their names, and their
    // sample rate.
    virtual int SoundSources() const = 0;
    virtual std::string SoundName(int source) const = 0;
    virtual double SoundRate() const = 0;
    // Source `source`'s sound from `from` to `to`, mono, and the time of its first sample, as captured
    // (before any sync offset). False if not all of it is kept.
    virtual bool Sound(int source, double from, double to, std::vector<float>* out, double* start) = 0;
};

// What a step found.
struct SyncStep {
    std::vector<std::string> found;  // for the log: a line for each thing measured in this step
    bool moved = false;              // the values to apply changed
    bool done = false;               // nothing more to measure in this setup
    AutoSyncValues values;           // what the measure answers, once it has
    std::string state;               // for the log, when moved or done: what the values rest on
};

class SyncMeasure {
public:
    virtual ~SyncMeasure() = default;

    // Up to `now` (performance-counter seconds): measures what `inputs` holds by then.
    virtual SyncStep Step(double now, SyncInputs& inputs) = 0;
};
