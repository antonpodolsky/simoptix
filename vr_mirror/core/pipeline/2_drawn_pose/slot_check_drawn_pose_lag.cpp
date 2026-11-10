// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "pipeline/2_drawn_pose/slot_check_drawn_pose_lag.h"

#include <algorithm>
#include <cmath>

namespace {

    // Share of the overlapping texels left out for lack of detail (see ReprojectionError). 0.5 cleared
    // the slow corner on real images completely but cost e2e's dust replay a frame at the queue's
    // start (worst frame 0.26-0.29 deg, a fail); 0.4 too (4.2% from the wrong pose).
    constexpr float kLeastDetail = 0.3f;

    // Row-major rotation taking rays of the eye at head pose `to` into the eye at head pose `from`.
    void EyeToEye(const Quat& from, const Quat& to, const Quat& cant, float m[3][3]) {
        const Quat fromEye = Mul(from, cant), toEye = Mul(to, cant);
        ToMatrix(Normalize(Mul(Conj(fromEye), toEye)), m);
    }

    // The pose halfway between two poses.
    Quat Halfway(const Quat& a, Quat b) {
        if (Dot(a, b) < 0) {
            b = {-b.x, -b.y, -b.z, -b.w};
        }
        return Normalize({a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w});
    }

    // Thumbnail `t` (rendered at the pose `m` maps from) seen along ray (x, y, -1) of another eye:
    // bilinear, into out[0..channels). False if the ray falls outside it.
    bool Sample(
        const std::vector<float>& t,
        const float m[3][3],
        float x,
        float y,
        const ThumbView& v,
        float sx,
        float sy,
        float* out
    ) {
        const int w = v.width, h = v.height, ch = v.channels;

        const float px = m[0][0] * x + m[0][1] * y - m[0][2];
        const float py = m[1][0] * x + m[1][1] * y - m[1][2];
        const float pz = m[2][0] * x + m[2][1] * y - m[2][2];

        if (pz > -1e-4f) {
            return false;
        }
        const float fx = (px / -pz + v.left) / sx - 0.5f, fy = (v.up - py / -pz) / sy - 0.5f;
        if (fx < 0 || fy < 0 || fx > w - 1.001f || fy > h - 1.001f) {
            return false;
        }

        const int x0 = int(fx), y0 = int(fy);
        const float ax = fx - x0, ay = fy - y0;
        const float* a = &t[(size_t(y0) * w + x0) * ch];
        const float* b = a + size_t(w) * ch;

        for (int k = 0; k < ch; k++) {
            out[k] = (a[k] * (1 - ax) + a[k + ch] * ax) * (1 - ay) + (b[k] * (1 - ax) + b[k + ch] * ax) * ay;
        }

        return true;
    }

    constexpr int kMaxTexels = 64 * 32;

    // Per texel, on the stack (nothing allocated per frame): both re-projected thumbnails' values
    // summed over the channels, whether each has one, and the squared difference. Then each texel's
    // detail, and a scratch copy to sort.
    struct Texels {
        float sumA[kMaxTexels], sumC[kMaxTexels], errors[kMaxTexels], detail[kMaxTexels], kept[kMaxTexels];
        bool okA[kMaxTexels], okC[kMaxTexels];
    };

    // Fills the sums, who has a value and the errors of every texel, seen through the two rotations
    // (mp for `prev`, mc for `cur`). Returns how many texels both thumbnails have.
    int Reproject(
        const std::vector<float>& prev,
        const float mp[3][3],
        const std::vector<float>& cur,
        const float mc[3][3],
        const ThumbView& v,
        Texels* texels
    ) {
        const int w = v.width, h = v.height, ch = v.channels;
        const float sx = (v.left + v.right) / w, sy = (v.up + v.down) / h;  // tangent per texel
        int overlap = 0;

        for (int j = 0; j < h; j++) {
            const float y = v.up - (j + 0.5f) * sy;

            for (int i = 0; i < w; i++) {
                const float x = -v.left + (i + 0.5f) * sx;
                const int t = j * w + i;
                float a[4], c[4];

                texels->okA[t] = Sample(prev, mp, x, y, v, sx, sy, a);
                texels->okC[t] = Sample(cur, mc, x, y, v, sx, sy, c);
                texels->sumA[t] = texels->sumC[t] = texels->errors[t] = 0;

                for (int k = 0; k < ch; k++) {
                    if (texels->okA[t]) {
                        texels->sumA[t] += a[k];
                    }
                    if (texels->okC[t]) {
                        texels->sumC[t] += c[k];
                    }
                    texels->errors[t] += (a[k] - c[k]) * (a[k] - c[k]);
                }

                overlap += texels->okA[t] && texels->okC[t];
            }
        }

        return overlap;
    }

    // The local gradient of the summed values `s` at texel (i, j) of a w x h grid, using the neighbours
    // that have a value (`ok`).
    float GradientAt(const float* s, const bool* ok, int i, int j, int w, int h) {
        auto Diff = [&](int t0, int t1, float span) {
            return ok[t0] && ok[t1] ? (s[t1] - s[t0]) / span : 0.0f;
        };

        const int t = j * w + i;
        const int l = i > 0 && ok[t - 1] ? t - 1 : t, r = i < w - 1 && ok[t + 1] ? t + 1 : t;
        const int u = j > 0 && ok[t - w] ? t - w : t, d = j < h - 1 && ok[t + w] ? t + w : t;
        const float gx = r != l ? Diff(l, r, float(r - l)) : 0,
                    gy = d != u ? Diff(u, d, float((d - u) / w)) : 0;

        return std::sqrt(gx * gx + gy * gy);
    }

    // The detail of each texel both thumbnails have (the larger gradient of the two), in the order of
    // the texels. Returns how many.
    int MeasureDetail(Texels* texels, int w, int h) {
        int count = 0;
        for (int j = 0; j < h; j++) {
            for (int i = 0; i < w; i++) {
                const int t = j * w + i;
                if (texels->okA[t] && texels->okC[t]) {
                    texels->detail[count++] = std::max(
                        GradientAt(texels->sumA, texels->okA, i, j, w, h),
                        GradientAt(texels->sumC, texels->okC, i, j, w, h)
                    );
                }
            }
        }

        return count;
    }

    // Collects into `kept` the errors of the `count` texels with detail, leaving out the kLeastDetail share
    // with the least. Returns how many are kept.
    int KeepDetailed(Texels* texels, int w, int h, int count) {
        std::copy(texels->detail, texels->detail + count, texels->kept);
        const int least = int(count * kLeastDetail);
        std::nth_element(texels->kept, texels->kept + least, texels->kept + count);
        const float cut = texels->kept[least];
        int n = 0;
        for (int t = 0, k = 0; t < w * h; t++) {
            if (!texels->okA[t] || !texels->okC[t]) {
                continue;
            }
            if (texels->detail[k++] >= cut) {
                texels->kept[n++] = texels->errors[t];
            }
        }

        return n;
    }

}

float ReprojectionError(
    const std::vector<float>& prev,
    const Quat& prevPose,
    const std::vector<float>& cur,
    const Quat& curPose,
    const ThumbView& v
) {
    const int w = v.width, h = v.height, ch = v.channels;
    if (prev.size() != size_t(w) * h * ch || cur.size() != prev.size() || w * h > kMaxTexels || ch > 4) {
        return -1;
    }

    // Both thumbnails re-projected halfway, to the pose between the two, so both get the same
    // interpolation blur. Re-projecting only one blurs only that one (head motion is a fraction
    // of a texel per frame), so the smallest rotation always matched best: on real images the
    // matcher took on-time frames for frames 1-3 slots late (measured on four recordings: the
    // image's motion fitted 0.1 of the head's instead of 1.0; 132-612 false re-timings per
    // window, none with this).
    const Quat mid = Halfway(prevPose, curPose);
    float mp[3][3], mc[3][3];
    EyeToEye(prevPose, mid, v.cant, mp);
    EyeToEye(curPose, mid, v.cant, mc);

    Texels texels;
    if (Reproject(prev, mp, cur, mc, v, &texels) * 2 < w * h) {
        return -1;
    }

    // Only texels with detail: the share of the overlap with the least (local gradient of either
    // thumbnail) is left out first. Featureless texels (a dark cockpit, the sky) match whatever
    // the rotation; in a slow corner they filled the better-matching half below, so the evidence
    // was noise and the matcher re-timed on-time frames (jitter at 18:31 of race D, where the
    // detail was in the scenery and cars). On thumbnails rebuilt from the recordings
    // (vr_mirror\recording_analysis\replica.py), with the timing gate: 25 on-time frames re-timed by up to
    // 3 slots there -> 5 by one slot; the real dust queue 11 -> 0, its late frames found 43 -> 49
    // of 101; without the gate, cornering and the 1:57 hiccup 13 -> 0 each.
    const int count = MeasureDetail(&texels, w, h);
    const int n = KeepDetailed(&texels, w, h, count);

    // Then the better-matching half only: content that moves on its own (scenery through the
    // windscreen, the wheel, HUD panels) doesn't follow any head rotation and would otherwise
    // decide the match.
    float* kept = texels.kept;
    const int keep = std::max(1, n / 2);
    std::nth_element(kept, kept + keep, kept + n);
    double sum = 0;
    for (int k = 0; k < keep; k++) {
        sum += kept[k];
    }
    return float(sum / keep);
}

float RotationSensitivity(const std::vector<float>& thumb, const Quat& pose, const ThumbView& view) {
    const float kDeg = 0.25f;  // small enough to stay in the linear range of a 1-degree texel
    const float half = kDeg * 3.14159265f / 360;
    const Quat yaw = {0, std::sin(half), 0, std::cos(half)}, pitch = {std::sin(half), 0, 0, std::cos(half)};
    const float a = ReprojectionError(thumb, Mul(pose, yaw), thumb, pose, view);
    const float b = ReprojectionError(thumb, Mul(pose, pitch), thumb, pose, view);
    if (a < 0 || b < 0) {
        return 0;
    }
    return 0.5f * (a + b) / (kDeg * kDeg);
}

bool TimingGate::Update(double arrival, double lead, double period) {
    bool sign = false;

    if (lead > 0) {
        // Deviation from the usual lead, modulo a display period: a reading a whole period high
        // (the arrival on the other side of a slot step) is no deviation.
        const double dev = baseCount_ ? std::remainder(lead - baseline_, period) : 0;
        devs_[devHead_] = dev;
        devHead_ = (devHead_ + 1) % kDevs;
        devCount_ = std::min(devCount_ + 1, kDevs);

        double sorted[kDevs];
        std::copy(devs_, devs_ + devCount_, sorted);
        std::nth_element(sorted, sorted + devCount_ / 2, sorted + devCount_);

        if (baseCount_ >= 20 && devCount_ == kDevs && std::fabs(sorted[kDevs / 2]) > kShift) {
            sign = true;
        } else if (baseCount_ < 20 || std::fabs(dev) < kShift) {
            // The usual lead: a slow average of the undisturbed frames (the first 20 set it).
            baseCount_ = std::min(baseCount_ + 1, 1000);
            baseline_ += (baseCount_ == 1 ? lead - baseline_ : dev) * std::max(0.02, 1.0 / baseCount_);
        }
    }

    if (sign) {
        lastSign_ = arrival;
    }
    open_ = arrival - lastSign_ <= kHold;
    return open_;
}

int LagFilter::Update(const float step[kLags][kLags]) {
    const int at = int(updates_ % kHistory);
    first_[at] = !started_;
    if (!started_) {
        // Start at lag 0 (the game keeping up), the others one change away.
        for (int b = 0; b < kLags; b++) {
            cost_[b] = b == 0 ? 0 : kChange;
        }
        started_ = true;
    }

    float next[kLags];
    for (int b = 0; b < kLags; b++) {
        next[b] = 1e30f;
        for (int a = 0; a < kLags; a++) {
            const float c = cost_[a] + step[a][b] + (a == b ? 0.0f : b == 0 ? kReturn : kChange);
            if (c < next[b]) {
                next[b] = c, from_[at][b] = int8_t(a);
            }
        }
    }

    const float low = *std::min_element(next, next + kLags);
    for (int b = 0; b < kLags; b++) {
        cost_[b] = next[b] - low;
    }
    lag_ = int(std::min_element(cost_, cost_ + kLags) - cost_);
    updates_++;

    // Re-trace the current run back from this frame's cheapest lag (earlier runs are final).
    int b = lag_;
    for (uint64_t i = updates_; i-- > 0 && updates_ - i <= kHistory;) {
        const int k = int(i % kHistory);
        hindsight_[k] = int8_t(b);
        if (first_[k]) {
            break;
        }
        b = from_[k][b];
    }

    return lag_;
}
