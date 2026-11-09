// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// CockpitDrawnPose: the slot check (SlotCheckDrawnPose, unchanged, run inside) plus a correction
// of each frame's pose measured from its image, while the game's frames arrive irregularly
// (setting drawn_pose 2). Logic only: frames, thumbnails, patches and predicted poses come in
// as parameters; it knows nothing about D3D, PVR or threads. adapters\d3d\drawn_pose_feed makes
// the patches and feeds it.
//
// Why: below 90 fps the game queues frames, and a frame placed from the wrong slot's pose jumps
// against the frames around it by the head's motion over the slots it is off. Choosing between
// slots from 64 x 32 thumbnails gets many of those wrong; leaving it out leaves every wrong slot
// (.agents\skills\dev\findings\e2e.md, "Tried and dropped: poses from the arrival timing alone"). Here each
// frame is
// first placed as the slot check says, then its fixed cockpit is compared with the previous
// frame's as placed (cockpit_drawn_pose_shift.h): what the cockpit moved beyond what the two poses
// explain is that frame's error, and its pose is corrected by it. Like a phone combining its
// gyroscope with its compass, the image decides from frame to frame and the head poses in the
// long run: the correction is pulled back to the poses with a time constant of kPullS (at most
// 3 deg/s, so pulling back never shows as a jump itself), so measuring errors can't add up into drift.
//
// Each frame's patches are made twice (Arrived): on the grid's own areas, which the next frame is
// compared with, and where the poses say the previous frame's grid content went, compared with
// that: a turning head moves the cockpit by up to 20 texels a frame, out of a fixed patch.
//
// While frames arrive regularly at 90 fps nothing is measured and no patches are made: every
// frame's pose is the slot check's. Any timing irregularity (a dropped or bunched frame, a slot
// jump, a skipped serial, the slot check's own timing gate) starts the correction; it fades in and
// out over kFadeS, and goes after kQuietS of regular frames. Where the cockpit is too plain to
// measure, moves out of reach, or its patches don't agree, the frame keeps the last correction,
// pulled back towards the slot check's pose. Its decisions are shared between the feeding thread
// and the output thread under mutex_, held only to copy a few values.
#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

#include "pipeline/2_drawn_pose/cockpit_drawn_pose_shift.h"
#include "pipeline/2_drawn_pose/slot_check_drawn_pose.h"

class CockpitDrawnPose : public DrawnPose {
public:
    // The pull back to the head poses (s), the fade in and out (s), and how long the timing has to be
    // regular before the correction fades out (s).
    static constexpr double kPullS = 0.3, kFadeS = 0.3, kQuietS = 0.5;

    ImageNeeds Needs() const override;
    void Reset() override;
    int Arrived(
        const GameFrame& f,
        const PoseLookup& poses,
        const EyeInfo& eye,
        double period,
        float (*areas)[4]
    ) override;

    void Look(
        const GameFrame& f,
        const std::vector<float>& thumb,
        const std::vector<float>& patches,
        const PoseLookup& poses,
        const EyeInfo& eye,
        double period
    ) override;

    // The slot check's slot (a corrected frame keeps the one it was corrected against) with the
    // frame's correction; until the frame is looked at, the last ones.
    Quat PoseOf(const FrameTimes& f, PoseSource& poses) const override;
    void PosesAt(const double* times, const Quat* heads, int n, PoseSource& poses, Quat* out) const override {
        slots_.PosesAt(times, heads, n, poses, out);
    }

    void
    PosesBetween(const double* times, const Quat* heads, int n, PoseSource& poses, Quat* out) const override {
        slots_.PosesBetween(times, heads, n, poses, out);
    }

    // The slot check's line, and "cockpit correction": the frames corrected, measured from their
    // image, and too plain or moving to measure, and the largest correction.
    void Report(StatsReport& r) override;
    // The slot check's description, then whether the frame was looked at and corrected, the slot it
    // was corrected against and the correction's size.
    void Describe(const FrameTimes& f, StatsReport& r) const override;

private:
    struct Decision {
        uint64_t serial = 0;
        int lag = 0;  // the slot check's lag it was corrected against
        bool corrected = false;
        Quat correction;  // head frame, applied after the slot's pose
    };

    static constexpr int kDecisions = 256;  // by serial % kDecisions: as long as frames are held (2.8 s)
    // A frame from its arrival to its Look (by serial % kArrivals): whether the timing was irregular
    // then, and where its second set of patches was made (Arrived).
    struct Arrival {
        uint64_t serial = 0;
        bool active = false;
        uint64_t prevSerial = 0;  // the frame whose patches the second set follows (0: none)
        float movedX[kCockpitPatches] = {}, movedY[kCockpitPatches] = {};  // their centres (tangents)
        bool inside[kCockpitPatches] = {};
    };

    static constexpr int kArrivals = 16;

    // Whether the timing is irregular now (Arrived).
    bool Irregular(const GameFrame& f, double period);
    // This frame's correction from its patches, after the slot check's Look.
    void Correct(
        const GameFrame& f,
        const std::vector<float>& patches,
        const PoseLookup& poses,
        const EyeInfo& eye,
        double period
    );

    // This frame's correction measured against the previous frame as shown: its pose `placed` (the slot
    // check's) times the result is where its cockpit lines up. False if it can't be measured.
    // dt: seconds since the frame before (the patches' disagreement follows over kDisagreeS).
    bool Measure(
        const std::vector<float>& patches,
        const Arrival& a,
        const Quat& placed,
        const EyeInfo& eye,
        double dt,
        Quat* aligned
    );
    // MeasureMoves: each patch's move against what the poses predict (`m`: this frame's eye into the
    // previous frame's). FollowDisagreement: each patch's slow average of how far it is from the fitted
    // rotation `omega`.
    void MeasureMoves(
        const std::vector<float>& patches,
        const Arrival& a,
        const Quat& m,
        const EyeInfo& eye,
        PatchMove* moves
    ) const;
    void FollowDisagreement(const PatchMove* moves, const Vec3& omega, double dt);
    // How far the runtime's predictions move over kEnvelopeSlots after `placed`, plus a slack.
    static double
    Envelope(const PoseLookup& poses, const GameFrame& f, const Quat& placed, int lag, double period);

    SlotCheckDrawnPose slots_;
    // Feeding thread only. Arrived (in arrival order).
    GameFrame lastArrived_;
    bool hasArrived_ = false;
    double lastIrregular_ = -1e9;
    Arrival arrivals_[kArrivals];
    // Look (in the same order, a little later).
    double fade_ = 0;          // 0..1: how far the correction is faded in
    double lastTime_ = 0;      // the last frame's arrival (0: none yet)
    Quat unfaded_;             // the correction before the fade: the filter's own state
    std::vector<float> prev_;  // the previous frame's own patches, the pose it was shown with, its serial
    Quat prevPlaced_;
    double prevTime_ = 0;
    uint64_t prevSerial_ = 0;
    bool hasPrev_ = false;
    float disagree_[kCockpitPatches][2] = {};  // each patch's misfit to the fit, averaged (texels)
    // Both threads, under mutex_ (written by the feeding thread only, which reads them without it).
    mutable std::mutex mutex_;
    Quat correction_;  // the last frame's correction (as shown: faded)
    Decision decisions_[kDecisions];
    struct Stats {
        int frames = 0;        // frames whose pose was corrected at all (the correction faded in)
        int measured = 0;      // ...of which measured from their own image (the rest kept the last, fading)
        int unmeasured = 0;    // frames with patches the cockpit was too plain, moving or far off to measure
        float largestDeg = 0;  // the largest correction applied (degrees)
    } stats_;
};
