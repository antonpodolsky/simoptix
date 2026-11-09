// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// MirrorView: one VR Mirror source in OBS: it draws the stabilized headset view itself, on OBS's
// own frame clock, from the frames of the frame source it is given (FrameSource).
//
// Why: a mirror app shows frames in a window on the headset's clock and OBS grabs that window on
// its own clock. The two run at almost the same rate, so for minutes at a time OBS's grabs sit
// right on the window's updates and timing noise decides which frame it gets: measured with frame
// stamps, OBS grabbed the same frame twice ~1450 times and missed ~1060 in 90 s. Here OBS asks
// for a frame at each of its ticks and gets the game frame for that tick's timestamp, warped to
// the head pose of that moment: no race, smooth camera motion, and no extra window to capture.
//
// How: a copy thread on its own D3D11 device copies each new frame's picture into a ring of shared
// textures as soon as it can (FrameRing, frame_ring.h), and marks a slot ready once the GPU has finished
// the copy. OBS's render callback opens those textures on OBS's device and draws the warp in an OBS
// effect (warp_effect.h). The same core (the frame source, the frame pipeline) as the mirror app,
// unchanged.
//
// It runs only while shown anywhere (program, preview, a projector): otherwise no frame source running and
// no threads. Knows nothing about OBS's settings or properties (plugin.cpp reads them). Threads: the
// constructor, the destructor and SetSettings on OBS's UI thread; Show and Hide on any; Tick and Render on
// OBS's graphics thread; Width and Height on any.
#pragma once

#include <obs-module.h>

#include <atomic>
#include <cstdint>
#include <memory>

#include "adapters/system/trace_writer.h"
#include "common/clock_sync.h"
#include "common/footprint.h"
#include "common/settings.h"
#include "frame_ring.h"
#include "pipeline/pipeline.h"
#include "session_worker.h"

class MirrorView {
public:
    MirrorView(std::unique_ptr<FrameSource> frames, const MirrorSettings& settings, bool debug);
    ~MirrorView();

    // Core's settings, and Debug (the frame-number stamp and the frame trace together).
    void SetSettings(const MirrorSettings& settings, bool debug);

    // OBS's show and hide: the frame source starts and stops with them.
    void Show() { wanted_ = true; }
    void Hide() { wanted_ = false; }
    // Always the OBS canvas's base size, so it fills it at 1:1.
    uint32_t Width() const { return width_; }
    uint32_t Height() const { return height_; }

    // OBS's video tick and render callbacks.
    void Tick();
    void Render();

private:
    void FollowCanvas();
    void ToggleTrace(bool on);
    PipelineConfig ConfigOf() const;
    void FeedDrawnPose();
    void RestartDelay();
    void StartStop();
    void Prepare(uint64_t tickNs);
    void ApplySettingChanges(const PipelineConfig& config);
    void ApplyFixedDelay(FramePicker& picker);
    void OpenSlotTexture(const FrameRing::SlotInfo& slot);
    void SetPictureNeed();
    void StartFootprint();
    void LogFootprint(double seconds);
    void LogStats(uint64_t tickNs);
    void DrawStamp();
    void DestroyTextures();

    std::unique_ptr<FrameSource> frameSource_;  // the frames and poses
    FrameRing ring_;
    // OBS's show/hide set `wanted_`; the graphics thread asks the session worker to start and stop
    // (StartStop), never waiting for it.
    SessionWorker session_{frameSource_.get(), &ring_};
    std::atomic<bool> wanted_{false};
    bool started_ = false;        // graphics thread: the session runs and its per-start state is set up
    uint64_t framesAtStart_ = 0;  // graphics thread: FrameSource::FramesArrived() when it started

    MirrorSettings settings_;
    // The OBS canvas's base size: written each tick on the graphics thread (FollowCanvas), read by OBS
    // from any thread (Width, Height).
    std::atomic<uint32_t> width_{2560}, height_{1440};
    bool stamp_ = false;
    bool trace_ = false;  // debug: write a frame trace (see ToggleTrace)

    // Graphics thread only.
    // stages 2-6 (vr_mirror\core\pipeline), built for the settings (RestartDelay, Prepare)
    Pipeline pipeline_;
    ClockSync clock_;
    gs_effect_t* effect_ = nullptr;
    gs_texture_t* textures_[FrameRing::kMaxSlots] = {};
    uint32_t textureGen_[FrameRing::kMaxSlots] = {};
    uint64_t lastTickNs_ = 0;  // the tick the warp below belongs to (several views may render one tick)
    WarpParams warp_ = {};
    int warpSlot_ = -1;
    int delaySmoothing_ = -1;      // the SmoothingKind the frame picker and ring are set for
    bool loggedStabilize_ = true;  // Stabilizes(settings) as last logged (a change is logged once)
    const DrawnPose* fedDrawnPose_ = nullptr;  // the pipeline's drawn pose the ring was handed last
    double outDelay_ = 0;                      // the frame picker's output delay as last logged (s)
    FrameRing::SlotInfo snapshot_[FrameRing::kMaxSlots];  // this tick's ring (Prepare)
    StoredFrame stored_[FrameRing::kMaxSlots];            // ...as the frame picker sees it
    uint32_t ticks_ = 0;
    TraceWriter tickTrace_;  // the per-tick part of the frame trace, while it is on (the pipeline's observer
                             // writes it: TickTraceSink)
    struct TickTraceSink : TraceSink {
        TraceWriter* writer = nullptr;
        void Line(const char* line) override { writer->Printf("%s\n", line); }
    } tickTraceSink_;

    int statTicks_ = 0, statRepeats_ = 0, statJumps_ = 0, statNoFrame_ = 0, statHeld_ = 0;
    double statStart_ = 0;     // source time the current 10 s stretch of stats began (0: not yet)
    uint64_t statFrames_ = 0;  // FrameSource::FramesArrived() then
    // Footprint (LogFootprint): the threads' cycle counts and the timestamp counter at the start of
    // the stretch, and the graphics thread's time in Prepare per tick (seconds).
    uint64_t footClock_ = 0, footPoll_ = 0, footCopy_ = 0;
    DurationSamples prepareTimes_;
};
