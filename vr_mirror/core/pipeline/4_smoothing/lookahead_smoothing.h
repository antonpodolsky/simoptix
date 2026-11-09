// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// LookaheadSmoothing: the view for a frame from the head poses around it, up to a fixed delay after
// it, for a recording that may run that far behind the game (setting smoothing 2, Look-ahead: half a
// second). A jerk-penalised path over the window a live output can see: the
// path stays close to the head and its jerk (third difference) is penalised with a time constant
// tau, so a steady turn, or a steadily changing one, is followed with no lag at all, while tremor
// and tracking noise are averaged away. tau varies with the head's speed (long while nearly still,
// short in turns), each moment with its own inside one solve, so consecutive frames share nearly the
// same path. The path is kept inside the images: each moment is checked against its drawn pose
// (image_room), and where it would pass kPlanRoom of its room, the moment gets the margin point as
// its target with a large weight, and the path is solved again (up to kPlanPasses times), so the jerk
// penalty eases it there and back ahead of time. Its last resort, for what the plan misses: the view
// stopped at the edge of the frame's images (StopAtEdge). Measured offline on 4.3 h of real head
// poses: .agents\skills\dev\findings\mirror_app.md, "Findings: other smoothing methods".
//
// Logic only: poses come in sampled at a fixed spacing; no runtime calls, no locking, and no
// allocation once its buffers have grown to the window (call from one thread per instance).
// LookaheadParams and LookaheadOutputDelayS stay inline here for e2e's runner, which links no core.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "common/rot.h"
#include "pipeline/4_smoothing/smoothing.h"
#include "pipeline/image_room/image_room.h"

// The tuning of the Look-ahead setting.
struct LookaheadParams {
    double delayS = 0.5;     // how far the recording runs behind: the poses seen after the frame (s)
    double tauStillS = 0.3;  // time constant while the head is nearly still (s)
    double tauMoveS = 0.05;  // ...and while it turns (s)
    double stillDeg = 5;     // below this turning speed the head counts as still (deg/s)
    double moveDeg = 100;    // from this speed on, the turning time constant (geometric in between)
};

// How long after a frame's arrival its view can be computed: the look-ahead delay, plus the ~10-20
// ms from arrival to the display slot the frame was rendered for, which the window is centred on.
// Hosts show each frame at least this late.
inline double LookaheadOutputDelayS() {
    return LookaheadParams().delayS + 0.03;
}

class LookaheadSmoother {
public:
    static constexpr double kSpeedSpan = 0.25;  // s: a moment's speed is the fastest turn within this of it

    // Samples needed before the frame (the window behind it) at spacing `dt`.
    static int Behind(const LookaheadParams& p, double dt) {
        return int(std::ceil(6 * p.tauStillS / dt)) + 4;
    }

    static int Ahead(const LookaheadParams& p, double dt) { return int(std::lround(p.delayS / dt)); }

    // `poses`: the head at frame time + i * dt for i = -behind .. +ahead (behind + ahead + 1 of them,
    // the frame's own at index `behind`). Returns the view for the frame (not roll-locked); PathView
    // then gives the solved path at the window's other moments.
    // targets, weights (optional, one per moment): what the path is pulled towards at each moment and
    // how hard (the head with weight 1 when not given): the edge planning moves a moment's target to
    // the edge margin with a large weight, and the jerk penalty shapes the way there.
    Quat View(
        const Quat* poses,
        int behind,
        int ahead,
        double dt,
        const LookaheadParams& p,
        const Quat* targets = nullptr,
        const double* weights = nullptr
    ) {
        const int m = behind + ahead + 1;
        Grow(m);

        // Each moment's time constant, from the fastest turn within kSpeedSpan of it.
        for (int i = 0; i < m; i++) {
            speed_[i] = i ? AngleBetween(poses[i - 1], poses[i]) / dt : 0;
        }
        const int span = int(std::lround(kSpeedSpan / dt));
        const double deg = 3.14159265358979 / 180;
        for (int i = 0; i < m; i++) {
            double peak = 0;
            for (int j = std::max(0, i - span); j <= std::min(m - 1, i + span); j++) {
                peak = std::max(peak, speed_[j]);
            }

            const double x = std::clamp((peak / deg - p.stillDeg) / (p.moveDeg - p.stillDeg), 0.0, 1.0);
            const double r = p.tauStillS * std::pow(p.tauMoveS / p.tauStillS, x) / dt;
            lambda_[i] = r * r * r * r * r * r;
        }

        // (I + M' diag(lambda) M) s = y on the rotation vectors relative to the frame's pose, M the
        // third difference: banded (3 below the diagonal), a[i * 4 + d] = A(i, i - d).
        std::fill(a_.begin(), a_.begin() + size_t(m) * 4, 0.0);
        for (int i = 0; i < m; i++) {
            a_[size_t(i) * 4] = weights ? weights[i] : 1;
        }
        const double c[4] = {-1, 3, -3, 1};
        for (int r = 0; r + 3 < m; r++) {
            const double l = lambda_[r + 1];  // the stencil's middle moment
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j <= i; j++) {
                    a_[size_t(r + i) * 4 + (i - j)] += l * c[i] * c[j];
                }
            }
        }

        for (int j = 0; j < m; j++) {  // Cholesky, in band form
            double d = a_[size_t(j) * 4];
            for (int k = std::max(0, j - 3); k < j; k++) {
                d -= Sq(a_[size_t(j) * 4 + (j - k)]);
            }
            d = std::sqrt(d);
            a_[size_t(j) * 4] = d;
            for (int i = j + 1; i < std::min(m, j + 4); i++) {
                double s = a_[size_t(i) * 4 + (i - j)];
                for (int k = std::max(0, i - 3); k < j; k++) {
                    s -= a_[size_t(i) * 4 + (i - k)] * a_[size_t(j) * 4 + (j - k)];
                }

                a_[size_t(i) * 4 + (i - j)] = s / d;
            }
        }

        const Quat inv = Conj(poses[behind]);
        center_ = poses[behind];
        m_ = m;
        for (int axis = 0; axis < 3; axis++) {
            for (int i = 0; i < m; i++) {
                y_[i] = 0;
            }
            for (int i = 0; i < m; i++) {
                if (i == behind && !targets) {
                    continue;
                }
                const Quat d = Normalize(Mul(inv, targets ? targets[i] : poses[i]));
                y_[i] = LogAxis(d, axis) * (weights ? weights[i] : 1);
            }

            for (int i = 0; i < m; i++) {  // forward, then back
                for (int k = std::max(0, i - 3); k < i; k++) {
                    y_[i] -= a_[size_t(i) * 4 + (i - k)] * y_[k];
                }
                y_[i] /= a_[size_t(i) * 4];
            }

            for (int i = m; i-- > 0;) {
                for (int k = i + 1; k < std::min(m, i + 4); k++) {
                    y_[i] -= a_[size_t(k) * 4 + (k - i)] * y_[k];
                }
                y_[i] /= a_[size_t(i) * 4];
            }

            for (int i = 0; i < m; i++) {
                path_[size_t(axis) * m + i] = y_[i];
            }
        }

        return PathView(behind);
    }

    // The solved path at window index i (0 .. behind + ahead) of the last View: the view the head's
    // path around the frame gives for that moment (not roll-locked). Close to, not the same as, what
    // that moment's own frame will get from its own window.
    Quat PathView(int i) const {
        const double v[3] = {path_[i], path_[size_t(m_) + i], path_[size_t(2) * m_ + i]};
        return Normalize(Mul(center_, Exp(v)));
    }

private:
    static double Sq(double v) { return v * v; }
    // One component of the rotation vector (axis * angle, radians) of a unit quaternion.
    static double LogAxis(Quat q, int axis) {
        if (q.w < 0) {
            q = {-q.x, -q.y, -q.z, -q.w};
        }
        const double s = std::sqrt(double(q.x) * q.x + double(q.y) * q.y + double(q.z) * q.z);
        const double k = s < 1e-12 ? 2.0 : 2 * std::atan2(s, double(q.w)) / s;
        return k * (axis == 0 ? q.x : axis == 1 ? q.y : q.z);
    }

    static Quat Exp(const double v[3]) {
        const double angle = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (angle < 1e-12) {
            return {};
        }
        const double s = std::sin(angle / 2) / angle;
        return {float(v[0] * s), float(v[1] * s), float(v[2] * s), float(std::cos(angle / 2))};
    }

    void Grow(int m) {
        if (int(speed_.size()) >= m) {
            return;
        }
        speed_.resize(m), lambda_.resize(m), y_.resize(m), a_.resize(size_t(m) * 4),
            path_.resize(size_t(m) * 3);
    }

    std::vector<double> speed_, lambda_, y_, a_;  // grown once to the window, then reused
    std::vector<double> path_;  // the last solve's path: 3 rotation-vector components x the window
    Quat center_;               // ...relative to the frame's pose
    int m_ = 0;
};

class LookaheadSmoothing : public Smoothing {
public:
    // The output is always level (its path is checked after levelling).
    void Reset() override {}  // no state between frames
    double DelayS() const override { return LookaheadOutputDelayS(); }
    void Window(double period, int* behind, int* ahead) const override;
    Smoothed Smooth(const SmoothingInput& in) override;

private:
    // Planning the path inside the images: a moment that would pass kPlanRoom of its room (inside
    // the last resort's kStopShare) gets the margin point as its target, weighted kPlanWeight against
    // the head's 1 (the jerk penalty's (tau / dt)^6 is ~8,000 in a fast turn); up to kPlanPasses
    // solves, as pulling one moment in can push its neighbours out.
    static constexpr float kPlanRoom = 0.9f;
    static constexpr double kPlanWeight = 1e4;
    static constexpr int kPlanPasses = 3;

    // One planning pass over the solved path: moves the targets of moments that near the edge. True if
    // a moment got a target for the first time this frame (the path should be solved again).
    bool Plan(const SmoothingInput& in, int n);

    LookaheadParams params_;
    LookaheadSmoother solver_;
    std::vector<Quat> targets_;    // per path moment: what the path is pulled towards (grown once)
    std::vector<double> weights_;  // ...and how hard
};
