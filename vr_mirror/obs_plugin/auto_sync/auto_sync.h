// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// AutoSync: puts the game's sound in step with a VR Mirror source's view by itself. Its measure
// (SyncMeasure, sync_measure.h) finds out which audio source has the game's sound and how much it has to
// move. From its first answer on, AutoSync moves it (AudioSync::SetGameSound), on top of the output delay
// that every audio source gets. Once the measure is done, what it found is kept (auto_sync_values.h) to
// start from in the next OBS session. Knows nothing about how the measure finds out.
//
// It measures once the game's frames run, and again whenever what the values depend on changes (SyncSetup,
// sync_setup.h: the game's frame rate or image size, a restart of the game's frames, OBS's audio sources,
// Windows' output device). A change while it measures starts the measuring over. Once the measure is done,
// it only watches the setup. Meanwhile the values from before apply.
//
// It runs only while the source is shown, and gives the measure what it looks at (SyncInputs): grey copies
// of the frames on the copy thread (FrameRing::SetGreyCopies), the last two seconds of each audio source's
// sound (AudioTap), and whether the player is driving (the telemetry). It steps the measure every 250 ms
// and reads the setup every 2 s, on a thread of its own at below-normal priority. Between sources shown,
// the thread sleeps.
//
// Made and destroyed with its VR Mirror source (OBS's UI thread). Start and Stop: OBS's graphics thread;
// never wait. The rest: its own thread.
#pragma once

#include <windows.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "audio_tap.h"
#include "common/clock_sync.h"
#include "shared/unique_handle.h"
#include "sync_measure.h"
#include "sync_setup.h"

class FrameRing;
class FrameSource;

class AutoSync : private SyncInputs {
public:
    // Frames from `source`, copied by `ring`, and the view's Debug setting `debug`, which its thread and the
    // telemetry it holds log by: all live until the AutoSync is destroyed.
    AutoSync(FrameSource* source, FrameRing* ring, const std::atomic<bool>* debug);
    AutoSync(const AutoSync&) = delete;
    AutoSync& operator=(const AutoSync&) = delete;
    ~AutoSync() override;

    // The view started: measures, or watches the setup, from the values kept for the frame source.
    void Start();
    // The view stopped: measuring pauses until it starts again.
    void Stop();
    // The view's Debug setting changed (any thread): the telemetry it holds hears it.
    void FollowDebug();

private:
    void Run();
    bool Watch();
    void Begin(const SyncSetup& setup, const std::string& change);
    void Step();
    void Finish(const SyncStep& step);
    void End();
    void ReleaseTelemetry();

    // SyncInputs: what the measure looks at.
    bool Driving(double at) override;
    bool Frames(double from, double to, std::vector<GreyFrame>* out) override;
    GreyLayout FrameLayout() const override;
    int SoundSources() const override;
    std::string SoundName(int source) const override;
    double SoundRate() const override;
    bool Sound(int source, double from, double to, std::vector<float>* out, double* start) override;

    FrameSource* source_;
    FrameRing* ring_;
    const std::atomic<bool>* debug_;
    UniqueHandle wake_;
    std::atomic<bool> alive_{true};
    std::atomic<bool> shown_{false};           // Start and Stop
    std::atomic<bool> holding_{false};         // the telemetry is held (Telemetry::Acquire)
    std::atomic<bool> telemetryDebug_{false};  // ...with Debug on, as told to it (FollowDebug)

    // The thread's own.
    bool measuring_ = false;
    bool framesRun_ = false;  // the game's frames run, as the setup last read had it
    bool loaded_ = false;     // values_ holds the values kept from before
    std::string sourceName_;
    std::unique_ptr<SyncMeasure> measure_;
    AutoSyncValues values_;  // applied: those kept from before, or what the measure answered since
    SyncSetupWatch watch_;
    SyncSetup measuredSetup_;  // the setup the measure runs in
    ClockSync clock_;          // the frame source's clock against the performance counter
    AudioTap tap_;

    std::thread thread_;
};
