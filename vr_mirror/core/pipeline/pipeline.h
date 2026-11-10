// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Pipeline: the glue that runs the output thread's stages for each output frame: the drawn pose's
// answers (2), smoothing (4, which keeps the view inside the images by itself) and warp (5), in that
// order (the frame picker, 3, it only holds: a host that picks frames on its own clock asks it,
// Picker, before Update). Nothing between the smoothing and the warp changes the view. It holds what
// belongs to no single stage: the step clock, the previous output, the head's path and its drawn
// poses for the smoothing, and the counters for traces. It decides nothing a stage
// decides, and knows no implementation: the
// builder (pipeline_builder.h) makes the stages and hands them in. Stage 1 (arrival timing) runs on
// another thread and is handed to its adapter instead (PimaxSource); the drawn pose is shared with
// the adapter that feeds it images on the copy device's thread (DrawnPoseFeed, through
// SharedDrawnPose). Knows nothing about PVR, windows, swap chains or OBS.
//
// Output thread only (OBS's graphics thread, the mirror app's main thread).
#pragma once

#include <memory>
#include <vector>

#include "common/pose_source.h"
#include "common/settings.h"
#include "pipeline/2_drawn_pose/drawn_pose.h"
#include "pipeline/3_frame_picker/frame_picker.h"
#include "pipeline/4_smoothing/smoothing.h"
#include "pipeline/5_warp/warp.h"
#include "pipeline/observer/observer.h"
#include "pipeline/pipeline_config.h"
#include "pipeline/pipeline_types.h"
#include "pipeline/stage_stats.h"

// One output frame's request.
struct OutputRequest {
    // the frame itself (TimesOf), or a host tick's own moment (MomentTimes)
    FrameTimes view;
    // the frame whose image is shown, when not `view` itself (a stand-in)
    const FrameTimes* image = nullptr;
    double now = 0;            // the poses' clock (source seconds): steps frames that aren't synced
    float aspect = 16.0f / 9;  // the output's width / height
    double tick = 0;           // the host's tick (source seconds), for the observer; 0: `now`
};

class Pipeline {
public:
    struct Stages {
        std::unique_ptr<FramePicker> framePicker;
        std::shared_ptr<DrawnPose> drawnPose;  // also fed by the host's DrawnPoseFeed
        std::unique_ptr<Smoothing> smoothing;
        std::unique_ptr<Warp> warp;
        std::unique_ptr<Observer> observer;  // debugging beside the stages
    };

    // Takes a fresh set of stages for `config`; all of the glue's state starts afresh too.
    void Start(Stages stages, const PipelineConfig& config);
    bool Started() const { return stages_.drawnPose != nullptr; }
    // Replaces one stage (its config value changed); the rest, and the glue's state, are kept.
    void Swap(std::unique_ptr<FramePicker> s, FramePickerKind k);
    void Swap(std::shared_ptr<DrawnPose> s, DrawnPoseKind k);
    void Swap(std::unique_ptr<Smoothing> s, SmoothingKind k);
    void Swap(std::unique_ptr<Warp> s, WarpKind k);
    void Swap(std::unique_ptr<Observer> s, ObserverKind k);
    // The observer, for a rebuild that keeps it (its frame count and trace go on).
    std::unique_ptr<Observer> TakeObserver() { return std::move(stages_.observer); }
    // Every stage's numbers since the last call (StatsReport), for the host's periodic log, and for
    // the observer. Output thread; the drawn pose's are taken under its own lock.
    void Report(StatsReport& r);

    // Debugging (the observer): where its trace goes (kept across rebuilds and observers; null: none),
    // the number the frame-number stamp shows for the last frame, and when the host presented it.
    void SetTraceSink(TraceSink* sink);
    uint32_t Stamp() const { return stages_.observer ? stages_.observer->Stamp() : 0; }
    void Presented(double t) {
        if (stages_.observer) {
            stages_.observer->Presented(t);
        }
    }

    const PipelineConfig& Config() const { return config_; }
    // Stage 3, for a host that picks stored frames on its own clock.
    FramePicker* Picker() { return stages_.framePicker.get(); }
    // Stage 2, for the host to hand to the adapter that feeds it images (DrawnPoseFeed); a new one
    // whenever its config value changes.
    const std::shared_ptr<DrawnPose>& SharedDrawnPose() const { return stages_.drawnPose; }

    // Once per output frame. The image is the game's left eye, filling its texture. With look-ahead
    // smoothing, call no earlier than the smoothing's DelayS after the frame's arrival. With an
    // `image` (the OBS plugin's ticks), the view follows `view`'s pose and the image is
    // re-projected from its own.
    WarpParams Update(const MirrorSettings& s, PoseSource& poses, const EyeInfo& eye, const OutputRequest& r);

    // Restart smoothing (stabilization switched): the smoothing's Reset, and no previous view.
    void Reset();

    // The followed image's pose and the view shown for the last frame (for traces and tests).
    const Quat& LastRaw() const { return lastRaw_; }
    const Quat& LastView() const { return lastView_; }
    // The pose the last image was re-projected from (its frame's pose).
    const Quat& LastImagePose() const { return lastImage_; }
    // The zoom used for the last frame.
    float Zoom() const { return zoom_; }
    // Frames where the smoothing's last resort had to stop the view at an edge (should be rare; for
    // traces and tests), and frames whose smoothing steered its path away from the edges.
    unsigned EdgeStops() const { return stops_; }
    unsigned EdgePlanned() const { return planned_; }

private:
    // How far after the frames' moments the shown images run (s), when an older image stands in
    // (each frame showing its own image: 0, not asked), so the look-ahead's path is checked against
    // the images that will be shown at its moments. `lead`: this tick's, the shown frame's moment
    // minus the tick's (DrawnPose::MomentOf: their timing, not their poses); `dt` the step (s).
    double ShownLead(double lead, float dt);

    // The poses of one output frame: the one the image was drawn with, the one followed, the view shown.
    struct FramePoses {
        Quat image, raw, view;
    };

    // Step: the smoothing's step for this frame (and the step clock moves on). FillPath: the head's path and
    // the shown images' poses around the frame, for the look-ahead. Record: what the observer is told
    // about the frame.
    float Step(const FrameTimes& f, const OutputRequest& r);
    void FillPath(
        SmoothingInput* in,
        PoseSource& poses,
        const FrameTimes& f,
        const OutputRequest& r,
        bool standIn,
        float dt
    );

    void Record(
        Observer& observer,
        const MirrorSettings& s,
        const FrameTimes& f,
        const OutputRequest& r,
        const FramePoses& poses,
        float edgeShiftDeg
    );

    Stages stages_;
    PipelineConfig config_;
    std::vector<double> times_;  // the smoothing window's sample times and heads (grown once)
    std::vector<Quat> heads_;
    std::vector<Quat> images_;              // ...the pose of the image shown at each moment
    std::vector<double> imageTimes_;        // ...the time it is looked up at, when an older image stands in
    double lastStep_ = 0, lastUpdate_ = 0;  // the step clock: the last synced frame's step, the last `now`
    bool hasView_ = false;
    Quat lastRaw_, lastView_, lastImage_;
    float zoom_ = 0;
    unsigned stops_ = 0;          // frames where the smoothing's last resort stopped the view at an edge
    unsigned planned_ = 0;        // frames whose smoothing steered its path away from the edges
    unsigned reportedStops_ = 0;  // stops_ at the last Report
    static constexpr int kLeadKept = 9;  // ticks whose leads ShownLead takes the median of
    double leads_[kLeadKept] = {};       // ...the last ones (a ring, leadCount_ written so far)
    int leadCount_ = 0;
    double shownLead_ = 0;       // ShownLead's answer: the median, eased
    TraceSink* sink_ = nullptr;  // the host's trace, for the observer (SetTraceSink)
};
