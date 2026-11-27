// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// HoverSyncMeasure: auto sync's measure of the game's sound from the hovers in LMU's menus (sync_measure.h).
// Most of LMU's menu items tick and light up when the pointer comes onto them. The tick is the surer of the
// two: it is short and sharp (HoverTick), while some highlights change the image only a little. So each
// tick an audio source has is found first, and then the frame before it in which a small part of the menu
// lit up (HoverHighlight). Each gives how much later than the highlight that source has the tick.
// HoverTally decides which source has the game's sound, how much it has to move, and when to stop.
//
// It looks only while the player isn't driving. Each tick is measured kAfterS after it, once the frames
// around it are in. Logic only: the frames and sound come in (SyncInputs). One thread.
#pragma once

#include <string>
#include <vector>

#include "hover_highlight.h"
#include "hover_tally.h"
#include "hover_tick.h"
#include "sync_measure.h"

class HoverSyncMeasure : public SyncMeasure {
public:
    // From the values kept from before, with the audio sources and frames `inputs` has, from `start`
    // (performance-counter seconds) on.
    HoverSyncMeasure(const AutoSyncValues& kept, SyncInputs& inputs, double start);

    SyncStep Step(double now, SyncInputs& inputs) override;

private:
    struct Heard {
        int source;
        HoverTickFound tick;
    };

    void Listen(double now, SyncInputs& inputs);
    std::string Measure(const Heard& heard, const SyncInputs& inputs);

    double start_;  // when measuring started (performance-counter seconds), for the log
    HoverTally tally_;
    HoverHighlight highlight_;
    double read_;                // the frames read up to (performance-counter seconds)
    std::vector<double> heard_;  // each source's sound searched for ticks up to (performance-counter seconds)
    std::vector<Heard> waiting_;  // ticks whose frames aren't all in yet, oldest first
    std::vector<GreyFrame> greys_;
    std::vector<float> sound_;
};
