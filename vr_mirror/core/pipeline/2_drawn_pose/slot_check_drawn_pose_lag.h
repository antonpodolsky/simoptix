// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// SlotCheckDrawnPose's helpers: how many display slots late each game frame arrived, judged from the
// images (the measures, the timing gate and the lag filter). Pure logic: thumbnails, head poses and
// eye geometry come in as parameters; it knows nothing about D3D, PVR or threads.
//
// Why: when the GPU falls behind (dust clouds, traffic), LMU queues up to two frames. Each is
// still rendered with the pose predicted for its own display slot, but arrives one or two slots
// later at the usual rhythm, so timing alone takes it for an on-time frame, and the image is
// re-projected from a pose 11-22 ms too new (measured in a real race: 4-6 px jumps at 1080p).
// Timing can't tell reliably (tried: .agents\skills\dev\findings\e2e.md, "Findings so far"). The images
// can: the rotation between
// two consecutive frames' render poses shows in how the picture moved. For each pair of
// candidate lags (previous frame, this frame) the previous thumbnail is re-projected by the
// rotation between those two poses and compared with this one. A forward filter turns those
// costs into one lag per frame: the cheapest path so far, with a penalty for changing the lag,
// so a lag that persists is recognised by its accumulated evidence, not just at its start.
// A host that shows frames later (look-ahead smoothing holds them up to a second) can take each
// frame's lag in hindsight instead: on the cheapest path through every frame judged since.
#pragma once

#include <cstdint>
#include <vector>

#include "common/rot.h"

// A thumbnail's view: the tangents of its edges in the eye camera (right and up positive:
// x from -left to right, y from up at the top row to -down at the bottom) and the eye's
// orientation relative to the head.
struct ThumbView {
    float left = 1, right = 1, up = 1, down = 1;
    Quat cant;
    int width = 64, height = 32, channels = 3;
};

// Mean squared difference between thumbnail `cur` (rendered with head pose `curPose`) and
// thumbnail `prev` (rendered with `prevPose`), both re-projected to the pose halfway between
// (so both get the same interpolation blur), over the texels both cover: of those the 70% with
// the most detail (featureless ones match any rotation), and of that the better-matching half
// (content moving on its own drops out). Thumbnails: rows top to bottom, `channels` floats per
// texel (at most 4), at most 64x32. -1 if fewer than half the texels overlap.
float ReprojectionError(
    const std::vector<float>& prev,
    const Quat& prevPose,
    const std::vector<float>& cur,
    const Quat& curPose,
    const ThumbView& view
);

// How much ReprojectionError grows per square degree of pose error for this picture (the mean of
// small yaw and pitch offsets). Near 0 for a featureless picture.
float RotationSensitivity(const std::vector<float>& thumb, const Quat& pose, const ThumbView& view);

// Whether the game's frame timing shows a queue, so that frames may be late: arrivals shifted
// against the display grid for several frames (the slot lead, taken modulo a display period,
// more than 3 ms off its usual value over the last 5 frames; it stayed 3-7 ms off through the
// late stretch of a race, from before the first late frame). Only while it is open may frames
// get a lag: otherwise the right lag is 0, and the slot check, on real pictures, picked
// wrong lags when it ran then. Measured on three sessions:
// - a single frame's lead often reads a whole period high (hence modulo a period),
// - dropped frames alone are no sign: lone drops are common, and a short hiccup (three late
//   arrivals, two of them past 1.5 periods) opened a drop-based gate, after which the matcher
//   re-timed 75 on-time frames (visible jitter);
// - as below, open 1-4% of a session, shut through that hiccup and through fast head movement
//   at a steady 90 fps, open from the third frame of the real queue.
class TimingGate {
public:
    void Reset() {
        baseCount_ = devCount_ = 0;
        lastSign_ = -1e9;
        open_ = false;
    }

    // Each new game frame, in order: arrival time, its slot lead (GameFrame) and the display
    // period (seconds). Returns whether the gate is open.
    bool Update(double arrival, double lead, double period);
    bool Open() const { return open_; }

private:
    static constexpr double kShift = 0.003;  // seconds the slot lead may stray from its usual value
    static constexpr double kHold = 1.0;     // seconds the gate stays open after the last sign
    static constexpr int kDevs = 5;          // frames the lead's deviation is the median of
    double baseline_ = 0, lastSign_ = -1e9;
    double devs_[kDevs] = {};
    int baseCount_ = 0, devCount_ = 0, devHead_ = 0;
    bool open_ = false;
};

class LagFilter {
public:
    static constexpr int kLags = 4;  // lag index i: rendered for the slot i display periods earlier

    static constexpr int kHistory = 256;  // updates whose hindsight lag is kept (2.8 s at 90 fps)

    // Ends the current run of frames (the next Update starts afresh); the hindsight lags of the
    // frames before stay as they were.
    void Reset() {
        for (float& c : cost_) {
            c = 0;
        }
        lag_ = 0;
        started_ = false;
    }

    // step[a][b]: cost (square degrees) of the step from the previous frame at lag a to this frame
    // at lag b. Returns this frame's lag index (from the frames up to it).
    int Update(const float step[kLags][kLags]);
    int Lag() const { return lag_; }
    // Updates so far: update i (0-based) is the i-th call of Update.
    uint64_t Updates() const { return updates_; }
    // Update `index`'s lag on the cheapest path through every update of its run since (up to the
    // newest, or the Reset that ended the run). False once it is older than kHistory updates.
    bool HindsightLag(uint64_t index, int* lag) const {
        if (index >= updates_ || updates_ - index > kHistory) {
            return false;
        }
        *lag = hindsight_[index % kHistory];
        return true;
    }

private:
    // Penalty per lag change: (0.17 deg)^2. Tuned on real pictures (thumbnails rebuilt from four
    // recordings): 0.004, tuned on e2e's clean fake pictures, let noise switch the lag; 0.03
    // re-timed no on-time frame in any of the false cases and cut the error in the real queue
    // from 0.130 to 0.077 deg rms (0.015: 0.064 there, but false re-timing elsewhere).
    static constexpr float kChange = 0.03f;
    // Penalty for going back to lag 0 (the game caught up): (0.06 deg)^2. Lower, as a queue
    // ends at once; with kChange the filter held the last lag for several on-time frames after
    // the real queue (27 re-timed on real pictures; 12 with this), and no false case got worse.
    static constexpr float kReturn = 0.004f;
    float cost_[kLags] = {};
    int lag_ = 0;
    bool started_ = false;
    // Per update, by index % kHistory: for each lag of that frame, the previous frame's lag on the
    // cheapest path to it; whether it started a run; its lag in hindsight (re-traced each Update).
    int8_t from_[kHistory][kLags] = {};
    bool first_[kHistory] = {};
    int8_t hindsight_[kHistory] = {};
    uint64_t updates_ = 0;
};
