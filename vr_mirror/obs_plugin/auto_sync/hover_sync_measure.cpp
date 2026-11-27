// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hover_sync_measure.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

    // Where a tick's highlight is looked for (s from the tick): the image comes before the sound, by up
    // to a few hundred milliseconds. A tick is measured kAfterS after it, once those frames are in.
    constexpr double kLitFrom = -0.4, kLitTo = 0.1, kAfterS = 0.25;
    // The sound is searched up to kLateS before now, as it reaches the audio sources late, kSlabS at a
    // time at least, from kBeforeS earlier for the rises at its start.
    constexpr double kLateS = 0.1, kSlabS = 0.1, kBeforeS = 0.05;
    // Sound older than this isn't kept by then: the search moves on.
    constexpr double kKeptS = 1.5;

    // Where the frame at `time` is from the tick at `when`, for the log.
    std::string FromTick(double time, double when) {
        char text[40];
        const double ms = (when - time) * 1000;
        snprintf(text, sizeof(text), "%.0f ms %s the tick", std::fabs(ms), ms >= 0 ? "before" : "after");
        return text;
    }

    // Why no highlight was found, for the log.
    std::string WhyNone(const HoverLook& look, double when) {
        char text[200];

        switch (look.result) {
            case HoverLook::Result::kNotHeld:
                if (look.heldTo == 0) {
                    return "no frames held";
                }

                return "the frames held only from " + FromTick(look.heldFrom, when) + " to "
                       + FromTick(look.heldTo, when);
            case HoverLook::Result::kTooFew:
                snprintf(text, sizeof(text), "only %d frames there", look.frames);
                return text;
            case HoverLook::Result::kTooSmall:
                snprintf(
                    text,
                    sizeof(text),
                    "the biggest step %.1f, under %.0f, ",
                    look.bestStep,
                    look.minStep
                );
                return text + FromTick(look.best, when);
            case HoverLook::Result::kNotStandingOut:
                snprintf(
                    text,
                    sizeof(text),
                    "the biggest step %.1f, under %.0f times the median %.1f of %d frames, ",
                    look.bestStep,
                    look.standOut,
                    look.median,
                    look.frames
                );
                return text + FromTick(look.best, when);
            case HoverLook::Result::kLit:
                break;
        }

        return "";
    }

    std::vector<std::string> SourceNames(const SyncInputs& inputs) {
        std::vector<std::string> names;

        for (int i = 0; i < inputs.SoundSources(); i++) {
            names.push_back(inputs.SoundName(i));
        }

        return names;
    }

}

HoverSyncMeasure::HoverSyncMeasure(const AutoSyncValues& kept, SyncInputs& inputs, double start)
    : start_(start),
      tally_(kept, SourceNames(inputs)),
      highlight_(inputs.FrameLayout()),
      read_(start),
      heard_(static_cast<size_t>(inputs.SoundSources()), start) {}

// Reads the frames and the sound since the last step while not driving, and measures the ticks whose frames
// are in.
SyncStep HoverSyncMeasure::Step(double now, SyncInputs& inputs) {
    SyncStep step;

    if (inputs.Driving(now)) {
        read_ = now;
        std::fill(heard_.begin(), heard_.end(), now);
        highlight_.Reset();
        waiting_.clear();
    } else {
        if (inputs.Frames(read_, now, &greys_)) {
            for (const GreyFrame& grey : greys_) {
                if (grey.time > read_) {
                    highlight_.Add(grey);
                    read_ = grey.time;
                }
            }
        }

        Listen(now, inputs);
    }

    while (!waiting_.empty() && now >= waiting_.front().tick.when + kAfterS) {
        step.found.push_back(Measure(waiting_.front(), inputs));
        waiting_.erase(waiting_.begin());
        step.moved = tally_.Update() || step.moved;
    }

    step.done = tally_.Done();
    step.values = tally_.Values();

    if (step.moved || step.done) {
        step.state = tally_.Describe();
    }

    return step;
}

// Each audio source's ticks since it was last searched.
void HoverSyncMeasure::Listen(double now, SyncInputs& inputs) {
    const double to = now - kLateS;
    const size_t before = waiting_.size();

    for (int i = 0; i < static_cast<int>(heard_.size()); i++) {
        double& from = heard_[i];
        from = std::max(from, to - kKeptS);
        double start = 0;

        if (to - from < kSlabS || !inputs.Sound(i, from - kBeforeS, to, &sound_, &start)) {
            continue;
        }

        const SoundStretch stretch = {sound_.data(), sound_.size(), start, inputs.SoundRate()};

        for (const HoverTickFound& tick : FindHoverTicks(stretch, from, to)) {
            waiting_.push_back({i, tick});
        }

        from = to;
    }

    if (waiting_.size() != before) {
        std::sort(waiting_.begin(), waiting_.end(), [](const Heard& a, const Heard& b) {
            return a.tick.when < b.tick.when;
        });
    }
}

// The highlight before one tick, and the lag between them into the tally; for the log, a line: when the
// tick came (s since measuring started) and how far it rose, and what the frames around it showed.
std::string HoverSyncMeasure::Measure(const Heard& heard, const SyncInputs& inputs) {
    const double when = heard.tick.when;
    const HoverLook look = highlight_.Find(when + kLitFrom, when + kLitTo);
    char tick[160];
    snprintf(
        tick,
        sizeof(tick),
        "a tick in '%s' at %.2f s (%.0f dB)",
        inputs.SoundName(heard.source).c_str(),
        when - start_,
        heard.tick.riseDb
    );

    if (look.result != HoverLook::Result::kLit) {
        return std::string(tick) + ", with no highlight before it: " + WhyNone(look, when);
    }

    tally_.AddTick(heard.source, when - look.best, heard.tick.riseDb);
    char text[400];
    snprintf(
        text,
        sizeof(text),
        "%s, %.0f ms after its highlight: a step of %.1f against the median %.1f of %d frames; the next "
        "%.1f, ",
        tick,
        (when - look.best) * 1000,
        look.bestStep,
        look.median,
        look.frames,
        look.secondStep
    );
    return text + FromTick(look.second, when);
}
