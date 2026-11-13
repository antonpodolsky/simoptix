// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "pipeline/pipeline_builder.h"

#include "pipeline/1_arrival_timing/grid_arrival_timing.h"
#include "pipeline/1_arrival_timing/reported_arrival_timing.h"
#include "pipeline/2_drawn_pose/arrival_slot_drawn_pose.h"
#include "pipeline/2_drawn_pose/cockpit_drawn_pose.h"
#include "pipeline/2_drawn_pose/reported_drawn_pose.h"
#include "pipeline/2_drawn_pose/slot_check_drawn_pose.h"
#include "pipeline/3_frame_picker/fixed_delay_picker.h"
#include "pipeline/3_frame_picker/held_frame_picker.h"
#include "pipeline/3_frame_picker/newest_frame_picker.h"
#include "pipeline/4_smoothing/instant_smoothing.h"
#include "pipeline/4_smoothing/lookahead_smoothing.h"
#include "pipeline/5_warp/rotation_warp.h"
#include "pipeline/observer/no_observer.h"
#include "pipeline/observer/stamp_trace_observer.h"

std::unique_ptr<ArrivalTiming> MakeArrivalTiming(ArrivalTimingKind kind) {
    switch (kind) {
        case ArrivalTimingKind::kGrid:
            return std::make_unique<GridArrivalTiming>();
        case ArrivalTimingKind::kReported:
            return std::make_unique<ReportedArrivalTiming>();
    }

    return nullptr;
}

std::unique_ptr<FramePicker> MakeFramePicker(FramePickerKind kind) {
    switch (kind) {
        case FramePickerKind::kFixedDelay:
            return std::make_unique<FixedDelayPicker>();
        case FramePickerKind::kNewestFrame:
            return std::make_unique<NewestFramePicker>();
        case FramePickerKind::kHeldFrame:
            return std::make_unique<HeldFramePicker>();
    }

    return nullptr;
}

std::shared_ptr<DrawnPose> MakeDrawnPose(DrawnPoseKind kind) {
    std::shared_ptr<DrawnPose> p;
    switch (kind) {
        case DrawnPoseKind::kSlotCheck:
            p = std::make_shared<SlotCheckDrawnPose>();
            break;
        case DrawnPoseKind::kArrivalSlot:
            p = std::make_shared<ArrivalSlotDrawnPose>();
            break;
        case DrawnPoseKind::kCockpit:
            p = std::make_shared<CockpitDrawnPose>();
            break;
        case DrawnPoseKind::kReported:
            p = std::make_shared<ReportedDrawnPose>();
            break;
    }

    if (p) {
        p->Reset();
    }
    return p;
}

std::unique_ptr<Smoothing> MakeSmoothing(SmoothingKind kind) {
    switch (kind) {
        case SmoothingKind::kInstant:
            return std::make_unique<InstantSmoothing>();
        case SmoothingKind::kLookahead:
            return std::make_unique<LookaheadSmoothing>();
    }

    return nullptr;
}

std::unique_ptr<Warp> MakeWarp(WarpKind kind) {
    switch (kind) {
        case WarpKind::kRotation:
            return std::make_unique<RotationWarp>();
    }

    return nullptr;
}

std::unique_ptr<Observer> MakeObserver(ObserverKind kind) {
    switch (kind) {
        case ObserverKind::kNone:
            return std::make_unique<NoObserver>();
        case ObserverKind::kStampTrace:
            return std::make_unique<StampTraceObserver>();
    }

    return nullptr;
}

double SmoothingDelayS(SmoothingKind kind) {
    return kind == SmoothingKind::kInstant ? 0 : LookaheadOutputDelayS();
}

void BuildPipeline(const PipelineConfig& c, Pipeline* p) {
    Pipeline::Stages stages;
    stages.framePicker = MakeFramePicker(c.framePicker);

    // The drawn pose is fed on the copy device's thread and keeps what it found so far.
    stages.drawnPose = p->Started() && p->Config().drawnPose == c.drawnPose ? p->SharedDrawnPose()
                                                                            : MakeDrawnPose(c.drawnPose);

    stages.smoothing = MakeSmoothing(c.smoothing);
    stages.warp = MakeWarp(c.warp);

    // The observer goes on too (its frame numbers and trace).
    stages.observer = p->Started() && p->Config().observer == c.observer ? p->TakeObserver()
                                                                         : MakeObserver(c.observer);

    p->Start(std::move(stages), c);
}

void ReconfigurePipeline(const PipelineConfig& c, Pipeline* p) {
    if (!p->Started()) {
        return BuildPipeline(c, p);
    }
    const PipelineConfig& now = p->Config();
    if (c.framePicker != now.framePicker) {
        p->Swap(MakeFramePicker(c.framePicker), c.framePicker);
    }
    if (c.drawnPose != now.drawnPose) {
        p->Swap(MakeDrawnPose(c.drawnPose), c.drawnPose);
    }
    if (c.smoothing != now.smoothing) {
        p->Swap(MakeSmoothing(c.smoothing), c.smoothing);
    }
    if (c.warp != now.warp) {
        p->Swap(MakeWarp(c.warp), c.warp);
    }
    if (c.observer != now.observer) {
        p->Swap(MakeObserver(c.observer), c.observer);
    }
}
