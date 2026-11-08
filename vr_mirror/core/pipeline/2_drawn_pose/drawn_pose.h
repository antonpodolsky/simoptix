// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Stage 2, drawn pose: the head pose each game frame's picture was drawn with, which the warp
// re-projects it from and the view follows; and, for moments of the head's path around a frame,
// the pose a picture shown then is drawn with (what the smoothing keeps its path inside). How an
// implementation finds it (Pimax's prediction for a slot, a judgement from the pictures, a
// correction measured on the cockpit, the measured head) is its own business: nothing of it
// crosses this interface. Interface and its inputs; implementations beside it. Knows nothing about
// D3D, PVR or threads: pictures and predicted poses come in as parameters
// (adapters\d3d\drawn_pose_feed makes the pictures on the GPU and feeds them).
//
// Two threads use it. Feeding, on the thread that owns the copy device: Arrived for each new game
// frame, in order (it may ask for patches of the eye), then Look with the pictures it asked for
// (Needs), in the same order. Answers, on the output thread: PoseOf and PosesAt. An implementation
// that looks at pictures guards what both threads touch with its own briefly held lock.
#pragma once

#include <cstdint>
#include <vector>

#include "common/pose_source.h"
#include "pipeline/pipeline_types.h"
#include "pipeline/stage_stats.h"

// The runtime's head pose predicted for a display slot (source time); false if it isn't known. The
// feeding's lookups (not counted in the hosts' prediction stats).
class PoseLookup {
public:
    virtual bool PredictedHeadAt(double slot, Quat* q) const = 0;

protected:
    ~PoseLookup() = default;
};

// The part of the eye the thumbnails cover (fractions of the eye area: u0, v0, u1, v1): the middle,
// where the cockpit and the road ahead are, clear of the edges a re-projection leaves empty. It also
// holds most of LMU's HUD panels (timing tower, relative board, dials: fixed in the cockpit, sharp
// and the same in every car; they move with it within 0.2 px per frame), which the trimmed match
// then leans on. Widened to 12-88% to hold them fully: no measurable gain in the replays (with
// panels in the fake), coarser texels; so not.
constexpr float kThumbArea[4] = {0.2f, 0.25f, 0.8f, 0.85f};

// Small grey pictures of several areas of the eye, side by side in one picture of `columns` x `rows`
// tiles of `size` x `size` texels (tile i in column i % columns, row i / columns); the areas per
// frame: Arrived. Values as displayed (the thumbnails' colours). count 0: none.
struct PatchLayout {
    int count = 0, columns = 1, rows = 1, size = 32;
    int samples = 4;  // samples per texel along each side (box average)
};

// What an implementation looks at of each frame's picture.
struct PictureNeeds {
    bool thumbnail = false;  // a 64x32 colour thumbnail of kThumbArea (3 floats per texel)
    PatchLayout patches;     // patches of the eye, for the frames Arrived asks them for
};

class DrawnPose {
public:
    virtual ~DrawnPose() = default;

    // Feeding (the copy device's thread). What it looks at; nothing by default.
    virtual PictureNeeds Needs() const { return {}; }
    // Forget the history (new session, other image source).
    virtual void Reset() {}
    // A new game frame, in order. Returns how many patches of the eye it wants of it (their areas in
    // `areas`: u0, v0, u1, v1, fractions of the eye; Needs().patches.count at most), 0 for none.
    virtual int
    Arrived(const GameFrame&, const PoseLookup&, const EyeInfo&, double /*period*/, float (*)[4]) {
        return 0;
    }

    // Frame `f`'s pictures, read back, in the order the frames arrived: its thumbnail (empty when
    // none is wanted or it couldn't be made) and its patches (empty when none were made).
    virtual void Look(
        const GameFrame& /*f*/,
        const std::vector<float>& /*thumb*/,
        const std::vector<float>& /*patches*/,
        const PoseLookup&,
        const EyeInfo&,
        double /*period*/
    ) {}

    // Answers (the output thread). The head pose frame `f`'s picture was drawn with, as known now
    // (it may change as later frames are looked at).
    virtual Quat PoseOf(const FrameTimes& f, PoseSource& poses) const = 0;
    // Where on the head's timeline frame `f`'s drawn pose sits (source seconds; where the look-ahead's
    // window is centred): a steady rhythm, never a slot judged from the picture. By default the
    // frame's own moment (FrameTimes::moment: the slot it arrived for), which Pimax's predictions are
    // for.
    virtual double MomentOf(const FrameTimes& f) const { return f.moment; }
    // The drawn pose at each of `n` moments `times` (in increasing order: display slots, a period
    // apart), given the head at those moments (`heads`) as the window sampled it.
    virtual void
    PosesAt(const double* times, const Quat* heads, int n, PoseSource& poses, Quat* out) const = 0;

    // The same for moments anywhere between display slots (`times` in increasing order, a period
    // apart, `heads` the head at the unshifted window moments): what a picture drawn there would have
    // been warped from, interpolated between the slots around.
    virtual void
    PosesBetween(const double* times, const Quat* heads, int n, PoseSource& poses, Quat* out) const = 0;

    // Its numbers since the last Report, for the hosts' periodic log (any thread).
    virtual void Report(StatsReport&) {}
    // Debugging (the frame trace): what it knows now of how frame `f`'s pose was found, as name/value
    // pairs (its own terms: a judged slot, a correction...); nothing by default. The output thread,
    // like PoseOf; only while an observer wants it (Observer::WantsNotes).
    virtual void Describe(const FrameTimes& /*f*/, StatsReport&) const {}
};
