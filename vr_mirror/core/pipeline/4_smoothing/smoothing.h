// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Stage 4, smoothing: the view to show for a frame: the camera follows the image's head pose with
// tremor, vibration and tracking noise smoothed away, and stays inside the images by itself: its own
// means first (Instant catches up, the look-ahead plans its path), then its own last resort, a stop at
// the edge (vr_mirror\core\pipeline\image_room: the room and the stop, stateless geometry). Nothing after
// it changes the view. A smoothing that looks ahead gets the head's path around the frame and the drawn
// pose of each of its moments. Its output is level (levelling stays inside each smoothing: its path is
// checked after levelling, and levelling twice would change numbers). Interface and its input;
// implementations beside it. Knows nothing about PVR, frames' images or threads.
//
// Per output frame the glue calls Window (how much of the head's path it needs), then Smooth.
#pragma once

#include "pipeline/pipeline_types.h"
#include "pipeline/stage_stats.h"

struct SmoothingInput {
    Quat followed;          // the followed image's pose
    float dt = 0;           // since the previous output frame (s), clamped to 1-100 ms
    PreviousView previous;  // the pipeline's last output
    // The pose the image shown with this view was drawn with, which the view must stay inside: the
    // followed image's, or an older image's standing in for it (the OBS plugin's ticks).
    Quat shown;
    ViewGeometry geometry;  // the output's, for the room
    // The head's path (Window): the head, and the pose of the image that will be shown with the
    // view, at moment + (i - behind) * period, i = 0 .. behind + ahead; null without a window.
    const Quat* heads = nullptr;
    const Quat* pathImages = nullptr;
    double period = 0;  // the path's spacing (s): the display's
};

// A smoothing's answer: the view (level, inside the frame's images), whether it had to
// steer its path away from the edges this frame, and whether its last resort had to stop the view at
// an edge and how far that moved it (degrees) (for traces, the log and tests).
struct Smoothed {
    Quat view;
    bool steered = false;
    bool stopped = false;
    float stopDeg = 0;
};

class Smoothing {
public:
    virtual ~Smoothing() = default;
    // Restart smoothing (stabilization switched, the view lock released). Not the full reset of a
    // new pipeline: Instant keeps its head-speed estimate, as before.
    virtual void Reset() = 0;
    // How long after a frame's arrival its view can be computed (s): the output delay hosts keep.
    virtual double DelayS() const = 0;
    // Samples of the head's path needed before and after the frame at spacing `period` (0, 0: none).
    virtual void Window(double period, int* behind, int* ahead) const = 0;
    // The frame's view.
    virtual Smoothed Smooth(const SmoothingInput& in) = 0;
    // Its numbers since the last Report, for the host's log.
    virtual void Report(StatsReport&) {}
};
