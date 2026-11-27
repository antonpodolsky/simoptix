// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "auto_sync.h"

#include <obs-module.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>

#include "adapters/d3d/frame_source.h"
#include "auto_sync_values.h"
#include "hover_sync_measure.h"
#include "shared/log.h"
#include "shared/telemetry/qpc_clock.h"
#include "shared/telemetry/telemetry.h"
#include "vr_mirror/obs_plugin/audio_sync.h"
#include "vr_mirror/obs_plugin/frame_ring.h"

namespace {

    constexpr DWORD kStepMs = 250, kWatchMs = 2000;

    // The frame sources measured in this OBS session (FrameSource::Name), with the setup they were measured
    // in.
    std::mutex g_settledMutex;
    std::map<std::string, SyncSetup> g_settled;

    void Apply(const AutoSyncValues& values) {
        if (values.hasSound) {
            AudioSync::SetGameSound(values.gameSound, values.soundCorrection);
        }
    }

}

AutoSync::AutoSync(FrameSource* source, FrameRing* ring, const std::atomic<bool>* debug)
    : source_(source),
      ring_(ring),
      debug_(debug) {
    wake_.Reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    thread_ = std::thread([this] {
        const ScopedLogGate gate(debug_);  // it logs as the view's Debug setting says

        // An exception escaping a thread would end OBS on the spot; log it and stop measuring.
        try {
            Run();
        } catch (...) {
            blog(LOG_WARNING, "[simoptix_vr_mirror] auto sync stopped by an unexpected error");
        }
    });
}

AutoSync::~AutoSync() {
    alive_ = false;
    shown_ = false;
    SetEvent(wake_.Get());

    if (thread_.joinable()) {
        thread_.join();
    }

    ReleaseTelemetry();
}

void AutoSync::Start() {
    if (!holding_.exchange(true)) {
        FollowDebug();
        ::Telemetry::Acquire();
    }

    shown_ = true;
    SetEvent(wake_.Get());
}

void AutoSync::Stop() {
    shown_ = false;
    SetEvent(wake_.Get());
    ReleaseTelemetry();
}

void AutoSync::ReleaseTelemetry() {
    if (holding_.exchange(false)) {
        FollowDebug();
        ::Telemetry::Release();
    }
}

// Tells the telemetry whether it is held with Debug on (Telemetry::HoldDebug): before it is asked for, so the
// reader's first pass follows it, before it is let go, and when Debug changes. Each change is told once.
void AutoSync::FollowDebug() {
    const bool hold = holding_ && debug_->load();

    if (telemetryDebug_.exchange(hold) != hold) {
        ::Telemetry::HoldDebug(hold);
    }
}

void AutoSync::Run() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));  // Windows' audio devices
    ULONGLONG nextWatch = 0;

    while (alive_) {
        // Until the game's frames run, the setup is read often, so measuring starts with them.
        const DWORD waitMs = !shown_ ? INFINITE : measuring_ || !framesRun_ ? kStepMs : kWatchMs;
        WaitForSingleObject(wake_.Get(), waitMs);

        if (!shown_) {
            End();
            nextWatch = 0;
            framesRun_ = false;
            continue;
        }

        if (GetTickCount64() >= nextWatch) {
            framesRun_ = Watch();
            nextWatch = GetTickCount64() + (framesRun_ ? kWatchMs : kStepMs);
        }

        if (measuring_) {
            Step();
        }
    }

    End();

    if (com) {
        CoUninitialize();
    }
}

// Reads the setup, and measures when the values no longer hold: once the game's frames run, if nothing was
// measured in this setup yet, and again when the setup changes. False while the game's frames don't run yet.
bool AutoSync::Watch() {
    const SyncSetup setup = watch_.Read(source_, *ring_);

    if (setup.frameRate == 0 || setup.imageWidth == 0) {
        return false;  // no game frames yet: nothing to measure on
    }

    std::string change;

    if (measuring_) {
        change = SetupChange(measuredSetup_, setup);

        if (change.empty()) {
            return true;
        }

        End();
    } else {
        std::lock_guard<std::mutex> lock(g_settledMutex);
        const auto settled = g_settled.find(source_->Name());

        if (settled != g_settled.end()) {
            change = SetupChange(settled->second, setup);

            if (change.empty()) {
                return true;
            }

            g_settled.erase(settled);
        }
    }

    Begin(setup, change);
    return true;
}

// A measuring starts from the values applied (at first, those kept from before): every audio source
// tapped, grey copies of the frames made, and nothing measured yet.
void AutoSync::Begin(const SyncSetup& setup, const std::string& change) {
    sourceName_ = source_->Name();

    if (!loaded_) {
        values_ = LoadAutoSyncValues(sourceName_.c_str());
        Apply(values_);
        loaded_ = true;
    }

    measuredSetup_ = setup;
    tap_.Attach();
    clock_.Reset();
    ring_->SetGreyCopies(true);

    SyncInputs& inputs = *this;
    measure_ = std::make_unique<HoverSyncMeasure>(values_, inputs, QpcNow());
    measuring_ = true;
    CoreLog(
        "auto sync: measuring%s%s%s, with %d audio source%s",
        change.empty() ? "" : " again (",
        change.c_str(),
        change.empty() ? "" : " changed)",
        tap_.Count(),
        tap_.Count() == 1 ? "" : "s"
    );
}

// The source was hidden, the setup changed, or the measuring is done: no more grey copies or sound.
void AutoSync::End() {
    if (!measuring_) {
        return;
    }

    ring_->SetGreyCopies(false);
    tap_.Detach();
    measure_.reset();
    measuring_ = false;
}

// Steps the measure, and applies what it answers when that moved.
void AutoSync::Step() {
    const double q0 = QpcNow();
    const double sourceNow = source_->Now();
    clock_.Sample(sourceNow, q0, QpcNow());
    const SyncStep step = measure_->Step(QpcNow(), *this);

    for (const std::string& line : step.found) {
        CoreLog("auto sync: %s", line.c_str());
    }

    if (step.moved) {
        values_ = step.values;
        Apply(values_);
        CoreLog("auto sync now, %s", step.state.c_str());
    }

    if (step.done) {
        Finish(step);
    }
}

// The measure is done: the values are kept, and the setup is watched from now on and measured again when
// it changes.
void AutoSync::Finish(const SyncStep& step) {
    SaveAutoSyncValues(sourceName_.c_str(), values_);
    CoreLog("auto sync done, %s", step.state.c_str());

    {
        std::lock_guard<std::mutex> lock(g_settledMutex);
        g_settled[sourceName_] = measuredSetup_;
    }

    End();
}

bool AutoSync::Driving(double at) {
    const SessionState session = ::Telemetry::SessionAt(at);
    return session.connected && session.hasCar && session.driving;
}

// The grey copies, on the performance counter's clock: the frames' own clock is the frame source's.
bool AutoSync::Frames(double from, double to, std::vector<GreyFrame>* out) {
    if (!clock_.Valid()) {
        return false;
    }

    const double offset = clock_.Offset();
    ring_->CopyGreys(from + offset, to + offset, out);

    for (GreyFrame& grey : *out) {
        grey.time -= offset;
    }

    return true;
}

GreyLayout AutoSync::FrameLayout() const {
    GreyLayout layout;
    layout.width = kGreyWidth;
    layout.height = kGreyHeight;
    std::copy(kGreyArea, kGreyArea + 4, layout.area);
    layout.eye = source_->Eye();
    return layout;
}

int AutoSync::SoundSources() const {
    return tap_.Count();
}

std::string AutoSync::SoundName(int source) const {
    return tap_.Name(source);
}

double AutoSync::SoundRate() const {
    return tap_.Rate();
}

bool AutoSync::Sound(int source, double from, double to, std::vector<float>* out, double* start) {
    return tap_.Copy(source, from, to, out, start);
}
