// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "pipeline/3_frame_picker/fixed_delay_picker.h"

#include <algorithm>
#include <cmath>

#include "pipeline/3_frame_picker/fixed_delay_picker_ahead.h"

namespace {

    // The frames of a display `period` that can arrive before the host's next tick, `tickPeriod`
    // apart (0 if the host has no regular ticks, or ticks at least as often as the display).
    int BetweenTicksSlots(double period, double tickPeriod) {
        return tickPeriod > period ? int(std::ceil(tickPeriod / period)) : 0;
    }

    // The newest frame among the store's `total` entries, copied or not (0: none).
    uint64_t NewestSerial(const StoredFrame* frames, int total) {
        uint64_t newest = 0;

        for (int i = 0; i < total; i++) {
            newest = std::max(newest, frames[i].serial);
        }

        return newest;
    }

}

int WarmupSlots(double smoothingDelayS, double period, double tickPeriod, int maxSlots) {
    return std::clamp(
        int(std::ceil((0.045 + smoothingDelayS) / period)) + BetweenTicksSlots(period, tickPeriod) + 3,
        4,
        maxSlots
    );
}

bool FixedDelayCopiesShownOnly(bool hostCan, double smoothingDelayS, double tickPeriod) {
    return hostCan && tickPeriod > 0 && smoothingDelayS >= kFixedDelayCopyDoneS;
}

int FixedDelaySlots(
    double copyDelayS,
    double smoothingDelayS,
    double period,
    double tickPeriod,
    bool copiesShownOnly,
    int maxSlots
) {
    const double outDelay = copyDelayS + smoothingDelayS;

    // Only the frames shown: those within the delay and one tick more, at most one a tick and one a
    // display period. 3 more for a late copy or a frame copied but never shown.
    if (copiesShownOnly) {
        const double shown = std::ceil((outDelay + tickPeriod) / std::max(period, tickPeriod));
        return std::clamp(static_cast<int>(shown) + 3, 4, maxSlots);
    }

    // Every frame: those inside the delay, those that can arrive before the host's next tick, and room
    // for a late copy.
    const double inDelay = std::ceil(outDelay / period);
    return std::clamp(static_cast<int>(inDelay) + BetweenTicksSlots(period, tickPeriod) + 3, 4, maxSlots);
}

void FixedDelayPicker::Restart(
    double smoothingDelayS,
    double period,
    double tickPeriod,
    int maxSlots,
    bool copiesShownOnly
) {
    smoothingDelay_ = smoothingDelayS;
    maxSlots_ = std::min(maxSlots, kMaxOrder);
    copyDelay_ = warmStart_ = 0;
    delays_.clear();
    delays_.reserve(1024);  // a value a copy: 2 s of warm-up at up to 400 Hz, and room to spare
    firstSerial_ = 0;
    std::fill(std::begin(measured_), std::end(measured_), 0);
    lastShown_ = 0;
    lastSlot_ = -1;
    lastTick_ = 0;
    tickPeriod_ = tickPeriod;
    slots_ = WarmupSlots(smoothingDelayS, period, tickPeriod, maxSlots_);
    copiesShownOnly_ = FixedDelayCopiesShownOnly(copiesShownOnly, smoothingDelayS, tickPeriod);
    shownOnlyFrom_ = 0;
    fixedPending_ = false;
    restarts_++;
}

int FixedDelayPicker::SlotsFor(double smoothingDelayS, bool copiesShownOnly) const {
    if (copyDelay_ == 0 || period_ <= 0) {
        return 0;
    }

    return FixedDelaySlots(
        copyDelay_,
        smoothingDelayS,
        period_,
        tickPeriod_,
        FixedDelayCopiesShownOnly(copiesShownOnly, smoothingDelayS, tickPeriod_),
        maxSlots_
    );
}

std::unique_ptr<ShowForecast> FixedDelayPicker::MakeForecast() const {
    return std::make_unique<FixedDelayAhead>();
}

TickRecord FixedDelayPicker::Record() const {
    TickRecord r;
    r.shown = lastShown_;
    r.tick = lastTick_;
    r.tickPeriod = tickPeriod_;
    r.period = period_;
    r.outDelay = OutDelay();
    r.delaySettled = copyDelay_ > 0;
    r.copiesShownOnly = copiesShownOnly_;
    r.restarts = restarts_;
    return r;
}

void FixedDelayPicker::Report(StatsReport& r) {
    if (!fixedPending_) {
        return;
    }
    fixedPending_ = false;
    r.Line("copy delay fixed");
    r.Value("ms", copyDelay_ * 1000, 1);
    r.Value("ms copy ready p95", fixedP95_ * 1000, 1);
}

void FixedDelayPicker::WarmUp(double tick, double period, const StoredFrame* frames, int total) {
    // The OpenXR source learns the display's period from the layer, which can come after Restart.
    slots_ = WarmupSlots(smoothingDelay_, period, tickPeriod_, maxSlots_);

    // The copies made before the restart were made another way (held for the next frame, say), so
    // they aren't measured.
    if (warmStart_ == 0) {
        warmStart_ = tick;
        firstSerial_ = NewestSerial(frames, total) + 1;
    }

    // Each copy once: it stays in the store for many ticks, and counting it at each of them gave the
    // ones kept longest too much weight (#33).
    for (int i = 0; i < total; i++) {
        const StoredFrame& f = frames[i];
        uint64_t& measured = measured_[f.serial % kMeasured];

        if (f.valid && f.serial >= firstSerial_ && measured != f.serial) {
            delays_.push_back(f.ready - f.arrival);
            measured = f.serial;
        }
    }

    if (tick - warmStart_ > 2.0 && delays_.size() > 50) {
        std::sort(delays_.begin(), delays_.end());
        const double p95 = delays_[size_t(delays_.size() * 0.95)];
        copyDelay_ = std::min(0.045, std::max(0.004, p95 + 0.002));
        fixedP95_ = p95;
        fixedPending_ = true;

        // Every frame copied until those copied before the forecast began are past (below).
        slots_ = FixedDelaySlots(copyDelay_, smoothingDelay_, period, tickPeriod_, false, maxSlots_);
        delays_.clear();

        // The forecast starts at the first frame after this tick (the host's copy thread sees the
        // settled record then). The frames copied before it are all past once each was due and the
        // next frame and tick came: the next frame arrives within about a display period, so three of
        // the longer period leave room.
        if (copiesShownOnly_) {
            shownOnlyFrom_ = tick + OutDelay() + 3 * std::max(period, tickPeriod_);
        }
    }
}

Pick FixedDelayPicker::Tick(double tick, double period, const StoredFrame* frames, int total) {
    period_ = period;

    // The delay from a game frame's arrival to its output: fixed after a short warm-up (every
    // change would show a frame twice or skip one), long enough that the copy is nearly always done.
    if (copyDelay_ == 0) {
        WarmUp(tick, period, frames, total);
    }

    if (shownOnlyFrom_ > 0 && tick >= shownOnlyFrom_) {
        slots_ = FixedDelaySlots(copyDelay_, smoothingDelay_, period, tickPeriod_, true, maxSlots_);
        shownOnlyFrom_ = 0;
    }

    const double outDelay = (copyDelay_ > 0 ? copyDelay_ : 0.040) + smoothingDelay_;

    // Ready entries in game-frame order.
    int count = 0;
    for (int i = 0; i < slots_ && i < total; i++) {  // not the entries being freed (SlotsInUse shrank)
        if (frames[i].valid) {
            order_[count++] = i;
        }
    }

    std::sort(order_, order_ + count, [&](int a, int b) { return frames[a].serial < frames[b].serial; });

    auto due = [&](int i) { return frames[i].smoothTime + outDelay; };
    int shown = -1;
    for (int k = 0; k < count; k++) {
        due_[k] = due(order_[k]);
        if (frames[order_[k]].serial == lastShown_) {
            shown = k;
        }
    }

    Pick p;
    p.moment = tick - outDelay;
    int chosen = -1;  // entry

    if (shown >= 0) {
        const int pick = FixedDelayStep(shown, due_, count, tick);
        p.kind = pick == shown ? PickKind::kRepeat : pick > shown + 1 ? PickKind::kJump : PickKind::kNext;
        chosen = order_[pick];
    } else {
        for (int k = 0; k < count; k++) {
            if (due(order_[k]) <= tick) {
                chosen = order_[k];
            }
        }

        p.kind = PickKind::kNext;
    }

    if (chosen < 0) {
        // Nothing due is ready (a copy running late): show the last image again, re-projected to
        // this tick's head pose, rather than an empty frame.
        if (lastSlot_ >= 0 && lastSlot_ < slots_ && lastSlot_ < total && frames[lastSlot_].valid
            && frames[lastSlot_].serial == lastShown_) {
            chosen = lastSlot_;
            p.kind = PickKind::kHeld;
        } else {
            p.kind = PickKind::kNone;
            return p;
        }
    }

    lastShown_ = frames[chosen].serial;
    lastSlot_ = chosen;
    lastTick_ = tick;
    p.index = chosen;
    p.due = due(chosen);
    return p;
}
