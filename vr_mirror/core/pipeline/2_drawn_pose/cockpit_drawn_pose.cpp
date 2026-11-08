// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "pipeline/2_drawn_pose/cockpit_drawn_pose.h"

#include <algorithm>
#include <cmath>

namespace {

    constexpr double kPi = 3.14159265358979;

    // A measured step of the correction may be at most how far the runtime's predictions move over this
    // many display slots (from the slot check's back; the sum of their steps, so their own noise counts
    // too), plus kSlackRad: a wrong slot is off by the predictions' motion over the slots it is off (the
    // true lag and the slot check's, up to 2 and 3), so a larger step is something else moving (scenery
    // while the head is still, a passing car filling the patches). The predictions' own steps: in e2e's
    // head tremor, predictions one slot apart differed by up to 0.4 degree where the head moved 0.05.
    constexpr int kEnvelopeSlots = 6;
    constexpr double kSlackRad = 0.05 * kPi / 180;
    // The pull back to the head poses (CockpitDrawnPose::kPullS) moves the picture by at most this much
    // (rad/s): pulling a large correction back at the time constant's full speed moved the picture by
    // 0.1 degree a frame and more where the slot error was 3-4 degrees (fast glances two slots behind,
    // e2e), itself a visible jump on every frame (offline: 6.3% of frames against 1.4%). Not beyond
    // kPlausibleRad over the predictions' motion (CockpitDrawnPose::Correct): a correction that large is
    // no slot error, and goes back at the time constant's speed.
    constexpr double kPullRadS = 3 * kPi / 180;
    constexpr double kPlausibleRad = 1.5 * kPi / 180;
    // A patch whose move kept disagreeing with the rotation the others agree on by more than kDisagree
    // texels (averaged over kDisagreeS) is left out of the fit: scenery moving slowly past a patch's edge,
    // too little to stand out in any one frame, added up through the correction (e2e: 0.12 degree a frame
    // with the head still while the car cornered).
    constexpr float kDisagree = 0.25f;
    constexpr double kDisagreeS = 0.5;
    constexpr double kMaxGapS = 0.1;  // frames further apart than this aren't compared

    // Each frame's patches: the grid's own areas (kCockpitAreas, rows 0-3: compared with the next frame),
    // then the same areas where the poses say the previous frame's content went (rows 4-7: compared
    // with the previous frame's own areas), so that a turning head doesn't carry the cockpit out of them.
    constexpr int kStride = kCockpitColumns * kCockpitPatchSize;
    constexpr size_t kGridFloats = size_t(kStride) * kCockpitRows * kCockpitPatchSize;
    const PatchLayout kLayout = {
        2 * kCockpitPatches,
        kCockpitColumns,
        2 * kCockpitRows,
        kCockpitPatchSize,
        4,
    };

    double AngleRad(const Quat& a, const Quat& b) {
        const Vec3 v = RotationVector(Mul(Conj(a), b));
        return std::sqrt(double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z);
    }

    // The share t of a small rotation (towards the identity at 0).
    Quat Partial(const Quat& q, double t) {
        const Vec3 v = RotationVector(q);
        return FromRotationVector({float(v.x * t), float(v.y * t), float(v.z * t)});
    }

    // The display slot `slots` periods before arrival slot `slot` (0 when that is unknown).
    double SlotBefore(double slot, int slots, double period) {
        return slot > 0 ? slot - slots * period : 0;
    }

    // A ray of the eye at tangents (x, y) (right and up positive) through rotation `m`, as tangents.
    bool Project(const Quat& m, float x, float y, float* px, float* py) {
        const Vec3 r = Rotate(m, {x, y, -1});
        if (r.z > -1e-4f) {
            return false;
        }
        *px = r.x / -r.z, *py = r.y / -r.z;
        return true;
    }

    // Patch i's centre as tangents of `eye` (x right, y up), and a texel's size in tangents.
    void PatchGeometry(int i, const EyeInfo& eye, float* x, float* y, float* tx, float* ty) {
        const float* a = kCockpitAreas[i];
        const float spanX = eye.leftTan + eye.rightTan, spanY = eye.upTan + eye.downTan;
        *x = -eye.leftTan + 0.5f * (a[0] + a[2]) * spanX;
        *y = eye.upTan - 0.5f * (a[1] + a[3]) * spanY;
        *tx = (a[2] - a[0]) * spanX / kCockpitPatchSize;
        *ty = (a[3] - a[1]) * spanY / kCockpitPatchSize;
    }

    // Rays of the eye at head pose `cur` into the eye at head pose `prev`.
    Quat EyeToEye(const Quat& prev, const Quat& cur, const Quat& cant) {
        return Normalize(Mul(Conj(Mul(prev, cant)), Mul(cur, cant)));
    }

}

PictureNeeds CockpitDrawnPose::Needs() const {
    PictureNeeds n;
    n.thumbnail = true;
    n.patches = kLayout;
    return n;
}

void CockpitDrawnPose::Reset() {
    slots_.Reset();
    prev_.reserve(kGridFloats);  // copied into without reallocating
    hasArrived_ = false;
    lastIrregular_ = -1e9;
    for (Arrival& a : arrivals_) {
        a = {};
    }
    fade_ = lastTime_ = prevTime_ = 0;
    unfaded_ = {};
    for (auto& d : disagree_) {
        d[0] = d[1] = 0;
    }
    hasPrev_ = false;
    prevSerial_ = 0;

    std::lock_guard<std::mutex> lock(mutex_);
    correction_ = {};
    for (Decision& d : decisions_) {
        d = {};
    }
}

bool CockpitDrawnPose::Irregular(const GameFrame& f, double period) {
    if (!hasArrived_) {
        return false;
    }
    const GameFrame& p = lastArrived_;
    const double gap = f.time - p.time;

    // A dropped frame (a gap of two periods or more), bunched frames, a skipped or repeated slot, a
    // frame the host never saw.
    if (gap > 1.5 * period || gap < 0.5 * period) {
        return true;
    }
    if (f.slotTime > 0 && p.slotTime > 0 && std::fabs(f.slotTime - p.slotTime - period) > 0.25 * period) {
        return true;
    }

    return f.serial != p.serial + 1;
}

int CockpitDrawnPose::Arrived(
    const GameFrame& f,
    const PoseLookup& poses,
    const EyeInfo& eye,
    double period,
    float (*areas)[4]
) {
    slots_.Arrived(f, poses, eye, period, areas);
    if (slots_.GateOpen() || Irregular(f, period)) {
        lastIrregular_ = f.time;
    }
    lastArrived_ = f;
    hasArrived_ = true;
    Arrival& a = arrivals_[f.serial % kArrivals];
    a = {};
    a.serial = f.serial;
    a.active = f.time - lastIrregular_ < kQuietS;

    // The patches, only while the timing is irregular (or the correction still fades out).
    if (!(a.active || fade_ > 0)) {
        return 0;
    }
    for (int i = 0; i < kCockpitPatches; i++) {
        std::copy(kCockpitAreas[i], kCockpitAreas[i] + 4, areas[i]);
    }

    // Where the previous frame's patches went: through the previous frame as shown and this one at the
    // slot check's latest lag with the last correction (this frame's own lag isn't decided yet; what
    // is left is measured). Without a previous frame, its own areas again.
    Quat cur;
    const bool moved = hasPrev_ && f.time - prevTime_ < kMaxGapS && f.slotTime > 0
                       && poses.PredictedHeadAt(
                           SlotBefore(f.slotTime, slots_.LastSlotsBefore(), period),
                           &cur
                       );

    const Quat back = moved ? Conj(EyeToEye(prevPlaced_, Mul(cur, correction_), eye.hmdToEye)) : Quat();
    const float spanX = eye.leftTan + eye.rightTan, spanY = eye.upTan + eye.downTan;
    for (int i = 0; i < kCockpitPatches; i++) {
        float x, y, tx, ty, mx, my;
        PatchGeometry(i, eye, &x, &y, &tx, &ty);
        if (!Project(back, x, y, &mx, &my)) {
            mx = x, my = y;
        }
        const float du = (mx - x) / spanX, dv = -(my - y) / spanY;
        const float* home = kCockpitAreas[i];
        float* m = areas[kCockpitPatches + i];
        m[0] = home[0] + du, m[1] = home[1] + dv, m[2] = home[2] + du, m[3] = home[3] + dv;
        a.movedX[i] = mx, a.movedY[i] = my;

        // Off the eye: not compared (the sampler would repeat its edge).
        a.inside[i] = m[0] >= 0 && m[1] >= 0 && m[2] <= 1 && m[3] <= 1;
    }

    a.prevSerial = moved ? prevSerial_ : 0;
    return 2 * kCockpitPatches;
}

void CockpitDrawnPose::Look(
    const GameFrame& f,
    const std::vector<float>& thumb,
    const std::vector<float>& patches,
    const PoseLookup& poses,
    const EyeInfo& eye,
    double period
) {
    slots_.Look(f, thumb, patches, poses, eye, period);
    Correct(f, patches, poses, eye, period);
}

bool CockpitDrawnPose::Measure(
    const std::vector<float>& patches,
    const Arrival& a,
    const Quat& placed,
    const EyeInfo& eye,
    double dt,
    Quat* aligned
) {
    if (prev_.size() != kGridFloats || patches.size() != 2 * kGridFloats || a.prevSerial != prevSerial_) {
        return false;
    }

    // Rays of this frame's eye (at `placed`, the slot check's pose, with the last correction kept) into
    // the previous frame's eye (as it was shown): what is measured is the step from the last correction.
    const Quat cant = eye.hmdToEye;
    const Quat m = EyeToEye(prevPlaced_, Mul(placed, correction_), cant);
    const float h = 1e-3f;  // radians, for the moves' derivatives
    const Quat steps[3] = {
        FromRotationVector({h, 0, 0}),
        FromRotationVector({0, h, 0}),
        FromRotationVector({0, 0, h})
    };

    PatchMove moves[kCockpitPatches];
    MeasureMoves(patches, a, m, eye, moves);

    // Patches that kept disagreeing with the rotation the others agree on are left out of the fit
    // (still measured, to see when they agree again).
    PatchMove fitted[kCockpitPatches];
    for (int i = 0; i < kCockpitPatches; i++) {
        fitted[i] = moves[i];
        fitted[i].ok = moves[i].ok && std::hypot(disagree_[i][0], disagree_[i][1]) <= kDisagree;
    }

    Vec3 omega;
    int agreeing = 0;
    if (!FitCorrection(fitted, kCockpitPatches, &omega, &agreeing)) {
        return false;
    }
    FollowDisagreement(moves, omega, dt);

    // The step in the eye's frame, after the last correction in the head's: placed * c * cant * e =
    // placed * (c * cant e cant^-1) * cant.
    *aligned = Normalize(Mul(correction_, Mul(Mul(cant, FromRotationVector(omega)), Conj(cant))));
    return true;
}

void CockpitDrawnPose::MeasureMoves(
    const std::vector<float>& patches,
    const Arrival& a,
    const Quat& m,
    const EyeInfo& eye,
    PatchMove* moves
) const {
    const float h = 1e-3f;  // radians, for the moves' derivatives
    const Quat steps[3] = {
        FromRotationVector({h, 0, 0}),
        FromRotationVector({0, h, 0}),
        FromRotationVector({0, 0, h})
    };

    for (int i = 0; i < kCockpitPatches; i++) {
        if (!a.inside[i]) {
            continue;
        }

        // This frame's patch where the previous frame's content went (its centre: movedX, Y), against
        // the previous frame's own patch (centre x, y).
        float x, y, tx, ty;
        PatchGeometry(i, eye, &x, &y, &tx, &ty);
        const float cx = a.movedX[i], cy = a.movedY[i];

        // Where the poses say its content is in the previous frame's patch (texels; y down), and how
        // that moves with a correction of this frame's pose about each of the eye's axes.
        float px, py;
        if (!Project(m, cx, cy, &px, &py)) {
            continue;
        }
        const float gx = (px - x) / tx, gy = -(py - y) / ty;

        PatchMove& mv = moves[i];
        bool ok = true;
        for (int k = 0; k < 3 && ok; k++) {
            float qx, qy;
            ok = Project(Mul(m, steps[k]), cx, cy, &qx, &qy);
            mv.jacobian[0][k] = ok ? ((qx - x) / tx - gx) / h : 0;
            mv.jacobian[1][k] = ok ? (-(qy - y) / ty - gy) / h : 0;
        }

        if (!ok) {
            continue;
        }
        const size_t offset = size_t(i / kCockpitColumns) * kCockpitPatchSize * kStride
                              + size_t(i % kCockpitColumns) * kCockpitPatchSize;

        const PatchShift s = MeasurePatchShift(
            prev_.data() + offset,
            patches.data() + kGridFloats + offset,
            kStride,
            gx,
            gy
        );

        if (!s.ok) {
            continue;
        }
        mv.ok = true;
        mv.dx = s.dx - gx, mv.dy = s.dy - gy;
        std::copy(s.info, s.info + 3, mv.info);
    }
}

void CockpitDrawnPose::FollowDisagreement(const PatchMove* moves, const Vec3& omega, double dt) {
    const float follow = float(1 - std::exp(-dt / kDisagreeS));
    for (int i = 0; i < kCockpitPatches; i++) {
        const PatchMove& m = moves[i];
        if (!m.ok) {
            continue;
        }
        const float* j0 = m.jacobian[0];
        const float* j1 = m.jacobian[1];
        disagree_[i][0] += follow
                           * (m.dx - (j0[0] * omega.x + j0[1] * omega.y + j0[2] * omega.z) - disagree_[i][0]);

        disagree_[i][1] += follow
                           * (m.dy - (j1[0] * omega.x + j1[1] * omega.y + j1[2] * omega.z) - disagree_[i][1]);
    }
}

double CockpitDrawnPose::Envelope(
    const PoseLookup& poses,
    const GameFrame& f,
    const Quat& placed,
    int lag,
    double period
) {
    // How far the runtime's predictions move over kEnvelopeSlots (summed, so their own noise
    // counts): the most a measured step may be, and with kPlausibleRad the correction itself.
    double envelope = kSlackRad;
    Quat q0 = placed, q1;
    for (int k = 1; k <= kEnvelopeSlots; k++, q0 = q1) {
        if (!poses.PredictedHeadAt(SlotBefore(f.slotTime, lag + k, period), &q1)) {
            break;
        }
        envelope += AngleRad(q0, q1);
    }

    return envelope;
}

void CockpitDrawnPose::Correct(
    const GameFrame& f,
    const std::vector<float>& patches,
    const PoseLookup& poses,
    const EyeInfo& eye,
    double period
) {
    const Arrival& a = arrivals_[f.serial % kArrivals];
    const bool active = a.serial == f.serial && a.active;
    const double dt = lastTime_ > 0 ? std::clamp(f.time - lastTime_, 0.0, kMaxGapS) : period;
    lastTime_ = f.time;
    fade_ = active ? std::min(1.0, fade_ + dt / kFadeS) : std::max(0.0, fade_ - dt / kFadeS);

    int lag = 0;
    slots_.OwnSlotsBeforeOf(f.serial, &lag);
    Quat placed;
    const bool hasPose = f.slotTime > 0
                         && poses.PredictedHeadAt(SlotBefore(f.slotTime, lag, period), &placed);

    Quat correction;
    bool measured = false;
    if (fade_ > 0 && hasPose) {
        Quat aligned;
        const bool compared = !patches.empty() && a.serial == f.serial && hasPrev_
                              && f.time - prevTime_ < kMaxGapS;

        const double envelope = Envelope(poses, f, placed, lag, period);

        Quat step;  // the measured step from the last correction (head frame, after it)
        if (compared && Measure(patches, a, placed, eye, dt, &aligned)) {
            step = Normalize(Mul(Conj(correction_), aligned));
            measured = AngleRad(Quat(), step) <= envelope;
        }

        // Follow the picture from frame to frame; the head poses pull it back over kPullS, at most
        // kPullRadS (unless the correction is larger than the predictions' motion makes plausible).
        // The fade scales what is shown, not the filter: faded in or out, the picture moves by a share
        // of the correction per frame (kFadeS), and the filter keeps its state.
        const Quat u = measured ? Mul(unfaded_, step) : unfaded_;
        const double size = AngleRad(Quat(), u);
        double pull = size * (1 - std::exp(-dt / kPullS));
        if (size <= envelope + kPlausibleRad) {
            pull = std::min(pull, kPullRadS * dt);
        }
        unfaded_ = size > 0 ? Partial(u, 1 - pull / size) : u;
        correction = Partial(unfaded_, fade_);
    } else {
        unfaded_ = {};
        for (auto& d : disagree_) {
            d[0] = d[1] = 0;
        }
    }

    const double deg = AngleRad(Quat(), correction) * 180 / kPi;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        correction_ = correction;
        if (!patches.empty() && !measured && fade_ > 0 && hasPose) {
            stats_.unmeasured++;
        }
        if (fade_ > 0) {
            stats_.frames++;
            stats_.measured += measured;
            stats_.largestDeg = std::max(stats_.largestDeg, float(deg));
        }

        decisions_[f.serial % kDecisions] = {f.serial, lag, fade_ > 0, correction};
    }

    // This frame's own patches and the pose it is shown with, for the next.
    hasPrev_ = patches.size() == 2 * kGridFloats && hasPose;
    if (hasPrev_) {
        prev_.assign(patches.begin(), patches.begin() + kGridFloats);
        prevPlaced_ = Normalize(Mul(placed, correction));
        prevTime_ = f.time;
        prevSerial_ = f.serial;
    }
}

Quat CockpitDrawnPose::PoseOf(const FrameTimes& f, PoseSource& poses) const {
    int lag = 0;
    bool corrected = false;
    Quat correction;

    {
        // Its own correction once looked at, else the last one.
        std::lock_guard<std::mutex> lock(mutex_);
        const Decision& d = decisions_[f.serial % kDecisions];
        if (d.serial == f.serial && f.serial != 0) {
            correction = d.correction;
            corrected = d.corrected;
            lag = d.lag;
        } else {
            correction = correction_;
        }
    }

    // A corrected frame keeps the slot it was corrected against; the others the slot check's hindsight,
    // or the slot judged last.
    if (!corrected && !slots_.SlotsBeforeOf(f.serial, &lag)) {
        lag = slots_.LastSlotsBefore();
    }
    return PredictedPose(f, lag, correction, poses);
}

void CockpitDrawnPose::Describe(const FrameTimes& f, StatsReport& r) const {
    bool looked = false, corrected = false;
    int lag = 0;
    Quat c;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const Decision& d = decisions_[f.serial % kDecisions];
        looked = d.serial == f.serial && f.serial != 0;
        c = looked ? d.correction : correction_;
        corrected = looked && d.corrected;
        lag = d.lag;
    }

    slots_.Describe(f, r);  // a corrected frame keeps the slot it was corrected against (below) instead
    const double deg = 2 * std::atan2(std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z), std::fabs(c.w)) * 180
                       / 3.14159265358979;

    r.Line("cockpit correction");
    r.Value("looked at", looked ? 1 : 0);  // 0: drawn before its patches were measured (the last correction)
    r.Value("corrected", corrected ? 1 : 0);
    if (corrected) {
        r.Value("against slots before arrival", lag);
    }
    r.Value("correction deg", deg, 3);
}

void CockpitDrawnPose::Report(StatsReport& r) {
    slots_.Report(r);
    Stats s;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        s = stats_;
        stats_ = {};
    }

    if (s.frames == 0 && s.unmeasured == 0) {
        return;
    }
    r.Line("cockpit correction");
    r.Value("frames corrected", s.frames);
    r.Value("measured from the picture", s.measured);
    r.Value("with a cockpit too plain or moving to measure", s.unmeasured);
    r.Value("deg largest", s.largestDeg, 2);
}
