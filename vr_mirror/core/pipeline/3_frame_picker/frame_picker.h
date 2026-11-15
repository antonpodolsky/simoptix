// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Stage 3, frame picker: which stored game frame an output tick shows, when it is due, and the
// moment on the timeline that tick stands for. A host stores the frames it copied and asks the
// picker every time it could draw (the OBS plugin on each of OBS's ticks, the mirror app whenever its
// loop wakes); the picker also says how long the output runs behind the game (the delay the host's
// sound follows) and how many stored frames it needs. It can also hand out a forecast of its picks
// (ShowForecast), which tells from the frames as they arrive whether any tick will show one, so a
// host need not copy a frame no tick shows. Interface only; implementations beside it. Knows nothing
// about textures, OBS, PVR or threads: the store's entries come in as plain data (StoredFrame), times
// as source seconds.
#pragma once

#include <cstdint>
#include <memory>

#include "pipeline/pipeline_types.h"
#include "pipeline/stage_stats.h"

// Where the picker stands after its last tick: what its forecast needs. Plain data, so a host can hand
// a copy of it to another thread.
struct TickRecord {
    uint64_t shown = 0;         // the frame the last tick showed (serial)
    double tick = 0;            // that tick (source time; 0: no tick since the restart)
    double tickPeriod = 0;      // the host's output frame period (0: no regular ticks)
    double outDelay = 0;        // as OutDelay
    bool delaySettled = false;  // the output delay stays as it is until a restart
    uint32_t restarts = 0;      // changes at every Restart
};

// Whether any coming tick will show a frame, told from the frames as they arrive and the picker's
// newest TickRecord. One per host, on the thread that sees the frames arrive. Logic only.
class ShowForecast {
public:
    virtual ~ShowForecast() = default;
    // Asked once the frame after `frame` (`next`) has arrived, before either is copied: once for every
    // frame, in arrival order. `record`: the newest one the picker wrote. A forecast that can't tell
    // says true.
    virtual bool WillShow(const TickRecord& record, const StoredFrame& frame, const StoredFrame& next) = 0;
};

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
    // A forecast of its picks (ShowForecast), fed with Record after every tick; null if it can't
    // forecast them, and then every frame is copied.
    virtual std::unique_ptr<ShowForecast> MakeForecast() const { return nullptr; }
    // Where it stands after its last Tick.
    virtual TickRecord Record() const { return {}; }
    // Its numbers since the last Report, for the host's log.
    virtual void Report(StatsReport&) {}
};
