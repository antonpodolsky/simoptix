// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Stage 3, frame picker: which stored game frame an output tick shows, when it is due, and the
// moment on the timeline that tick stands for. A host stores the frames it copied and asks the
// picker every time it could draw (the OBS plugin on each of OBS's ticks, the mirror app whenever its
// loop wakes); the picker also says how long the output runs behind the game (the delay the host's
// sound follows), how many stored frames it needs, and whether a new frame will be shown at all.
// Interface only; implementations beside it. Knows nothing about textures, OBS, PVR or threads:
// the store's entries come in as plain data (StoredFrame), times as source seconds.
#pragma once

#include "pipeline/pipeline_types.h"
#include "pipeline/stage_stats.h"

class FramePicker {
public:
    virtual ~FramePicker() = default;
    // Starts again (the host started, or the smoothing changed): `delayS` how long after its
    // de-jittered arrival a frame is due at the earliest (the smoothing's output delay,
    // SmoothingDelayS, plus any presentation delay the host adds), `period` the display's as the
    // frame source knows it now, `tickPeriod` the host's output frame period (OBS's video settings; 0
    // for a host without regular ticks), `maxSlots` the most store entries the host has.
    virtual void Restart(double delayS, double period, double tickPeriod, int maxSlots) = 0;
    // One output tick at source time `tick`; `frames`: the host's store, `total` entries (the first
    // SlotsInUse are in use); `period` the display's.
    virtual Pick Tick(double tick, double period, const StoredFrame* frames, int total) = 0;
    // How long the output runs behind the frames' arrival (s): the copy delay plus the smoothing's.
    // When it changes (a picker that measures its delay fixed it), a host logs it and gives the
    // picker's Report right away.
    virtual double OutDelay() const = 0;
    // Store entries in use. It can change at any tick (the delay fixed, the display's period
    // learnt); a host resizes its store when it does.
    virtual int SlotsInUse() const = 0;
    // Whether any coming tick will show `frame`, asked once the frame after it (`next`) has arrived,
    // before either is copied: once for every frame, in arrival order. A picker that can't tell says
    // true.
    virtual bool WillShow(const StoredFrame& /*frame*/, const StoredFrame& /*next*/) { return true; }
    // Its numbers since the last Report, for the host's log.
    virtual void Report(StatsReport&) {}
};
