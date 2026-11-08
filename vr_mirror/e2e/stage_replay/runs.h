// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The runs stage_replay offers (runs_*.cpp), and the host emulation they and the golden dump share:
// RunHost drives a Pipeline over an input as one of the hosts would (the mirror app's window, or the OBS
// plugin's ticks). Each run prints its results and returns the number of failures (or -1 when an
// input can't be read).
#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "common/settings.h"
#include "pipeline/pipeline.h"
#include "pipeline/pipeline_config.h"
#include "replay_poses.h"

namespace stage_replay {

    // One host run: an input through a pipeline built for `config`.
    struct HostRun {
        const Input* input = nullptr;
        MirrorSettings settings;
        PipelineConfig config;
        bool ticks = false;  // the OBS plugin's ticks; else the mirror app's window
        double tickHz = 60;
        bool pickByStage = false;  // ticks: the frame picker (stage 3) over a store of copies; else each
                                   // tick takes the newest frame due a fixed 12 ms copy delay after arrival
        // output indices where stabilization goes off and on (with a reset)
        int stabilizeOff = -1, stabilizeOn = -1;
    };

    // What one output frame gave.
    struct HostOutput {
        int index = 0;  // the frame's index in the input (window) or the tick's
        WarpParams warp;
        const Pipeline* pipeline = nullptr;
        FrameTimes view;
        const FrameTimes* image = nullptr;  // the shown frame, when not `view` (ticks)
        int trueLag = 0;  // the slots before its arrival slot it was really drawn for (replays)
    };

    // LMU's left eye on the Dream Air, as the fake reports it (vr_mirror\e2e\fake_pvr), its 2 degree cant
    // included.
    extern const EyeInfo kEye;

    void RunHost(const HostRun& run, const std::function<void(const HostOutput&)>& output);

    // --pipeline: any mix of stages (flags named after PipelineConfig's fields and values) over the
    // replays or a motion spec, as the window or OBS ticks at `tickHz` (with the frame picker).
    int RunPipeline(
        const PipelineConfig& config,
        const MirrorSettings& settings,
        const std::vector<const Input*>& inputs,
        double tickHz
    );

    // --stage drawn_pose <replay .csv> [options]: the drawn pose on a replay's real thumbnails, its
    // poses scored against the ones the frames were really drawn with, as each host asks for them (was
    // lag_sim, then --stage frame_matching). argv[1] is the replay.
    int RunDrawnPose(int argc, char** argv, const PipelineConfig& config);

    // --stage frame_picker: the frame picker over OBS traces (pairs of _ticks.csv, _pimax.csv) and
    // synthetic ticks: the picks by kind, the delay fixed, how far each tick's moment trails the shown
    // picture's slot and how far the moments jump; the fixed delay unless --frame_picker says otherwise.
    int RunFramePicker(const std::vector<std::pair<std::string, std::string>>& traces, FramePickerKind kind);

    // --stage arrival_timing: the arrival timing over OBS traces' frame arrivals (_pimax.csv F rows).
    int RunArrivalTiming(const PipelineConfig& config, const std::vector<std::string>& pimaxTraces);

}
