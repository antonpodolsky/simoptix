// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The pipeline builder: makes each stage's implementation from its PipelineConfig value, and builds or
// reconfigures a Pipeline. The only code that knows every implementation (pipeline_builder.cpp);
// everything else sees the stages' interfaces. Make*, BuildPipeline and a reconfiguration that swaps
// a stage allocate: they run when the configuration changes, never per frame (ReconfigurePipeline
// with an unchanged configuration only compares).
#pragma once

#include <memory>

#include "pipeline/1_arrival_timing/arrival_timing.h"
#include "pipeline/2_drawn_pose/drawn_pose.h"
#include "pipeline/3_frame_picker/frame_picker.h"
#include "pipeline/4_smoothing/smoothing.h"
#include "pipeline/5_warp/warp.h"
#include "pipeline/pipeline.h"
#include "pipeline/pipeline_config.h"

// Each stage's implementation for `kind`.
std::unique_ptr<ArrivalTiming> MakeArrivalTiming(ArrivalTimingKind kind);
std::unique_ptr<FramePicker> MakeFramePicker(FramePickerKind kind);
// Shared: the output thread asks it, the host's DrawnPoseFeed feeds it.
std::shared_ptr<DrawnPose> MakeDrawnPose(DrawnPoseKind kind);
std::unique_ptr<Smoothing> MakeSmoothing(SmoothingKind kind);
std::unique_ptr<Warp> MakeWarp(WarpKind kind);
std::unique_ptr<Observer> MakeObserver(ObserverKind kind);

// The smoothing's output delay (Smoothing::DelayS) without making one: hosts size their warm-up and
// hold by it before a pipeline exists, and every tick. 0 for Instant.
double SmoothingDelayS(SmoothingKind kind);

// A fresh pipeline for `c`: every output-thread stage made anew, all state reset; the drawn pose,
// which a host's copy thread feeds, and the observer are kept if their values are unchanged.
void BuildPipeline(const PipelineConfig& c, Pipeline* p);
// Swaps only the stages whose value changed (the others keep their state); builds `p` if it has no
// stages yet. Hosts call it once per output frame.
void ReconfigurePipeline(const PipelineConfig& c, Pipeline* p);
