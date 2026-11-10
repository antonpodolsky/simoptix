// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "scores.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace stage_replay {

    namespace {

        constexpr double kDeg = 180.0 / 3.14159265358979;

        // Whether any corner of the output, warped as warp.hlsli's WarpSrc does, falls outside the image.
        bool Black(const WarpParams& p) {
            for (int corner = 0; corner < 4; corner++) {
                const float x = corner & 1 ? p.outTan[0] : -p.outTan[0],
                            y = corner & 2 ? p.outTan[1] : -p.outTan[1];
                float s[3];
                for (int r = 0; r < 3; r++) {
                    s[r] = p.m[r][0] * x + p.m[r][1] * y - p.m[r][2];
                }
                if (s[2] > -1e-4f) {
                    return true;
                }
                const float tx = s[0] / -s[2], ty = s[1] / -s[2];
                const float u = (tx + p.srcTan[0]) / (p.srcTan[0] + p.srcTan[1]);
                const float v = (p.srcTan[2] - ty) / (p.srcTan[2] + p.srcTan[3]);
                if (u < 0 || u > 1 || v < 0 || v > 1) {
                    return true;
                }
            }

            return false;
        }

        Vec3 Dir(const Quat& q) {
            return Rotate(q, {0, 0, -1});
        }

        // The rotation between two orientations (degrees), exact for nearly equal ones (2 acos(dot) reads
        // identical 7-digit quaternions 0.02 deg apart).
        double RotationDeg(const Quat& a, const Quat& b) {
            const Quat r = Mul(Conj(a), b);
            const double v = std::sqrt(double(r.x) * r.x + double(r.y) * r.y + double(r.z) * r.z);
            return 2 * std::atan2(v, std::fabs(double(r.w))) * kDeg;
        }

        double AngleDeg(Vec3 a, Vec3 b) {
            const double d = std::clamp(double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z, -1.0, 1.0);
            return std::acos(d) * kDeg;
        }

        // The prediction recorded for display slot `slot` (within a quarter period), if any.
        bool PredictionAt(const Input& in, double slot, Quat* q) {
            auto it = std::lower_bound(
                in.predictions.begin(),
                in.predictions.end(),
                slot - 0.5 * kPeriod,
                [](const replay::Prediction& p, double v) { return p.slot < v; }
            );

            if (it == in.predictions.end() || std::fabs(it->slot - slot) > 0.25 * kPeriod) {
                return false;
            }
            *q = it->q;
            return true;
        }

        // The view's jitter as e2e measures it: what is left of its direction after fitting a local
        // quadratic over +-50 ms (deliberate turns count as zero, tremor and shake fully), rms in degrees,
        // over the outputs whose image turned at speeds in [lo, hi) deg/s.
        double Jitter(
            const std::vector<double>& t,
            const std::vector<Vec3>& dirs,
            const std::vector<double>& speeds,
            double lo,
            double hi
        ) {
            double sum = 0;
            size_t count = 0;

            for (size_t k = 0; k < t.size(); k++) {
                if (speeds[k] < lo || speeds[k] >= hi) {
                    continue;
                }

                size_t a = k, b = k;
                while (a > 0 && t[k] - t[a - 1] <= 0.05) {
                    a--;
                }
                while (b + 1 < t.size() && t[b + 1] - t[k] <= 0.05) {
                    b++;
                }
                if (b - a < 4) {
                    continue;
                }

                double residual = 0;
                for (int c = 0; c < 3; c++) {
                    // Least squares y = p0 + p1 x + p2 x^2 around t[k]; the residual at x = 0 is y_k - p0.
                    double s[5] = {}, r[3] = {};
                    for (size_t i = a; i <= b; i++) {
                        const double x = t[i] - t[k], y = c == 0 ? dirs[i].x : c == 1 ? dirs[i].y : dirs[i].z;
                        double xp = 1;
                        for (int e = 0; e < 5; e++) {
                            s[e] += xp, xp *= x;
                        }
                        r[0] += y, r[1] += y * x, r[2] += y * x * x;
                    }

                    const double m[3][3] = {{s[0], s[1], s[2]}, {s[1], s[2], s[3]}, {s[2], s[3], s[4]}};
                    const double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
                                       - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                                       + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);

                    if (std::fabs(det) < 1e-30) {
                        return 0;
                    }
                    const double p0 = (r[0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
                                       - m[0][1] * (r[1] * m[2][2] - m[1][2] * r[2])
                                       + m[0][2] * (r[1] * m[2][1] - m[1][1] * r[2]))
                                      / det;

                    const double y0 = c == 0 ? dirs[k].x : c == 1 ? dirs[k].y : dirs[k].z;
                    residual += (y0 - p0) * (y0 - p0);
                }

                sum += residual * kDeg * kDeg;
                count++;
            }

            return count ? std::sqrt(sum / count) : 0;
        }

    }

    void Scores::Add(const HostOutput& o) {
        const Pipeline& p = *o.pipeline;
        outputs_++;
        black_ += Black(o.warp);
        clamps_ = p.EdgeStops(), planned_ = p.EdgePlanned();
        const Vec3 view = Dir(p.LastView()), image = Dir(p.LastRaw());
        lags_.push_back(float(AngleDeg(view, image)));

        // For the jitter: the view's direction per output, and how fast the image turned (deg/s).
        const double step = o.view.step;
        const double dt = times_.empty() ? 0 : step - times_.back();
        speeds_.push_back(hasImage_ && dt > 0 ? AngleDeg(image, imageDir_) / dt : 1e9);
        times_.push_back(step), dirs_.push_back(view);
        imageDir_ = image, hasImage_ = true;

        // The pose the image was warped from against the one it was drawn with.
        const FrameTimes& shown = o.image ? *o.image : o.view;
        Quat truth;
        if (shown.synced && shown.shownSlot > 0
            && PredictionAt(in_, shown.shownSlot - o.trueLag * kPeriod, &truth)) {
            posed_++;
            wrong_ += RotationDeg(p.LastImagePose(), truth) > 0.01;
        }
    }

    void Scores::Print(const char* name) const {
        std::vector<float> sorted = lags_;
        std::sort(sorted.begin(), sorted.end());
        const double n = double(std::max<size_t>(1, outputs_));
        printf(
            "  %-36s %5zu  black %5.2f%%  lag p99 %5.2f max %5.2f deg  jitter still %.4f small %.4f deg  "
            "edge limit %5.2f%%  planned %5.2f%%  wrong pose %5.2f%%\n",
            name,
            outputs_,
            100.0 * black_ / n,
            sorted.empty() ? 0 : sorted[size_t(sorted.size() * 0.99)],
            sorted.empty() ? 0 : sorted.back(),
            Jitter(times_, dirs_, speeds_, 0, 5),
            Jitter(times_, dirs_, speeds_, 5, 30),
            100.0 * clamps_ / n,
            100.0 * planned_ / n,
            posed_ ? 100.0 * wrong_ / posed_ : 0.0
        );
    }

}
