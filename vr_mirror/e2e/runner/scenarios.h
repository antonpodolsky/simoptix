// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The e2e scenarios: what each one runs (head motion, fake runtime and tool settings) and the
// limits it is graded against. Knows nothing about running the tool or measuring.
#pragma once

#include "common.h"
#include "patches.h"

struct Limits {
    double maxSmoothedRatio = -1;      // jitter of the tool's intended view relative to the head's
    double maxSmoothedJitterDeg = -1;  // ...or absolute (when the head itself barely jitters)
    // the view's jitter as a 60 fps recording shows it (held frames included)
    double maxRecordedJitterDeg = -1;
    // On-screen results depend on which head pose the game really renders with, which the fake
    // can only model (.agents\skills\dev\findings\e2e.md, "Open question: the fake game's pose"). They are
    // reported, and graded only where a limit is set.
    double maxJitterDeg = -1;     // high-frequency wobble of the view on screen (rms)
    double maxJitterRatio = -1;   // that wobble relative to the head's own
    double maxMismatchDeg = 1.5;  // on screen vs the tool's intended view (rms): catches gross breakage
    // ...in the worst captured frame (replays: the game's real poses are known)
    double maxWorstMismatchDeg = -1;
    // replays only: frames re-projected from another pose than they were rendered with
    double maxWrongPosePct = -1;
    double maxPoseErrorRmsDeg = -1;  // replays only: rms angle between those poses over all frames
    // replays only: frames whose pose error changed visibly from the frame before
    double maxPoseJumpPct = -1;
    double maxPoseDriftDeg = -1;  // the pose error's mean over the run: no drift
    // the view's width on screen against the one its zoom asks for (degrees between the quarter patches)
    double maxViewWidthErrorDeg = 0.2;
    double maxBlackEdgePct = 0;   // captured frames with black at an edge (view outside the game image)
    double maxEdgeClampPct = -1;  // frames where the last-resort edge limit acted
    int maxStalls = -1;           // view slowing to a near stop and speeding up again mid-turn
    double maxLagDeg = -1;        // how far the view may trail the head
    double minToolFps = 85;       // the tool must keep up with the game (90 fps)
    double maxMissedPct = 1;      // game frames the tool never showed
    // longest gap between the tool's frames (informational: the fake can be late too)
    double maxToolGapMs = -1;
    double maxObsRepeatPct = -1;   // 60 fps window-capture repeats (a Windows capture limit; informational)
    double maxTrackErrorDeg = -1;  // stabilization off: view vs head after best time alignment
    double maxRollDeg = -1;        // roll lock on: horizon tilt of the view
    // ...on the frames where the view looks more than 15° up or down (kPitchedDeg, runner.cpp)
    double maxPitchedRollDeg = -1;
    double minValidPct = 95;         // frames whose colors decoded to a valid direction
    double maxIdleCallsPerSec = -1;  // game_start_s: calls into the runtime per second while no game ran
    int maxStartMissed = -1;         // game_start_s: the game's first frames the tool didn't show
};

struct Scenario {
    std::string name, description, motion;
    // The scenario it is a smoothing variant of (its Instant name), and the smoothing it runs at
    // (0 Instant, 1 Smooth, 2 Smoothest; the tool's setting `smoothing` is one more).
    std::string base;
    int smoothing = 0;
    double seconds = 12;
    // The tool's window (client size, WxH). Small: what is read from it (direction, roll, black
    // borders) reads the same at 320x180 as larger, and replays are judged by poses
    // (.agents\skills\dev\reference\e2e_results.md, "Window size").
    std::string output = "320x180";
    std::map<std::string, std::string> fake;      // extra fake runtime settings
    std::map<std::string, std::string> settings;  // simoptix_vr_mirror.ini overrides
    Limits limits;
    // patches (Bit(Patch)) where no direction code counts as black; 0: unchecked
    int borderPatches = kEdgePatches;
    // The view's direction can be read on screen (the center patches show direction codes); not
    // in replays with real thumbnails, which cover the middle of the picture.
    bool viewOnScreen = true;
    // The poses each game frame was drawn with are what the tool can look up, so its pose measures
    // (frames from the wrong pose, visible pose jumps) are exact: replays, and the synthetic
    // scenarios whose game falls behind (queue_lag), since the fake predicts each slot once.
    bool posesExact = false;
    // Compare the view and smoothing measures of its Smooth and Smoothest runs with Instant
    // (compare.h); off where the view isn't what the scenario is about (only its frame measures).
    bool compareView = true;
    std::string error;  // why it can't run (e.g. a replay's thumbnails missing): reported as a fail
};

// The built-in synthetic scenarios, each at every smoothing (Instant keeps the name; then
// <name>_smooth and <name>_smoothest).
std::vector<Scenario> BuiltInScenarios();
// Every vr_mirror/e2e/replay/*.csv (a real session from the OBS plugin's frame trace) becomes a scenario.
void AddReplayScenarios(const fs::path& replayDir, std::vector<Scenario>* out);
