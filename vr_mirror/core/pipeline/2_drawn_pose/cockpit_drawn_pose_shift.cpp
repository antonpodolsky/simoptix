// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "pipeline/2_drawn_pose/cockpit_drawn_pose_shift.h"

#include <algorithm>
#include <cmath>

// Rows at v 0.14, 0.38, 0.62, 0.86 and columns at u 0.14, 0.32, 0.5, 0.68, 0.86; each 0.05 of the eye
// wide and 0.06 high (square in pixels: 125 of LMU's 2508 x 2084 eye).
const float kCockpitAreas[kCockpitPatches][4] = {
    {0.115f, 0.11f, 0.165f, 0.17f}, {0.295f, 0.11f, 0.345f, 0.17f}, {0.475f, 0.11f, 0.525f, 0.17f},
    {0.655f, 0.11f, 0.705f, 0.17f}, {0.835f, 0.11f, 0.885f, 0.17f}, {0.115f, 0.35f, 0.165f, 0.41f},
    {0.295f, 0.35f, 0.345f, 0.41f}, {0.475f, 0.35f, 0.525f, 0.41f}, {0.655f, 0.35f, 0.705f, 0.41f},
    {0.835f, 0.35f, 0.885f, 0.41f}, {0.115f, 0.59f, 0.165f, 0.65f}, {0.295f, 0.59f, 0.345f, 0.65f},
    {0.475f, 0.59f, 0.525f, 0.65f}, {0.655f, 0.59f, 0.705f, 0.65f}, {0.835f, 0.59f, 0.885f, 0.65f},
    {0.115f, 0.83f, 0.165f, 0.89f}, {0.295f, 0.83f, 0.345f, 0.89f}, {0.475f, 0.83f, 0.525f, 0.89f},
    {0.655f, 0.83f, 0.705f, 0.89f}, {0.835f, 0.83f, 0.885f, 0.89f},
};

namespace {

    constexpr int kN = kCockpitPatchSize, kHalf = kN / 2;
    // The weakest direction's mean squared gradient per texel (displayed values 0..1, per texel) below
    // which a patch is too plain to measure: about one 8-bit grey level per texel. A smooth gradient (the
    // sky, the e2e fake's direction codes) has detail in one direction only and stays under it.
    constexpr float kMinTexture = 1.5e-5f;
    // After the move, what is left of the difference may be at most this share of the patch's variance;
    // more: the content changed (a screen, a mirror, something passing).
    constexpr float kMaxMismatch = 0.25f;
    // The noise below which a patch's own residual isn't trusted (displayed value), and the precision no
    // single patch is credited beyond (texels): neighbouring texels are not independent, and one sharp
    // patch shouldn't outvote the others it agrees with.
    constexpr float kNoiseFloor = 0.002f, kBestPrecision = 0.02f;
    constexpr int kCoarseReach = 5;  // half-resolution texels searched either side of the guess: 10 texels
    constexpr int kIterations = 6;
    // Texels count less the further their residual is beyond this many times the residuals' median after
    // the first step (Cauchy weights); a patch where under kMinCounted of the texels count is mostly
    // something moving.
    constexpr float kRobustScale = 3, kMinCounted = 0.5f;
    // How far a patch may be from the rotation most agree on to count as agreeing: three times their
    // median misfit, but at least kAgreeFit and at most kAgreeMost (texels; 0.18 degree each).
    constexpr float kAgreeFit = 0.3f, kAgreeMost = 0.8f;
    // A pair of patches' fit leans this little (texels^2 per rad^2) towards no rotation: only where the
    // pair can't tell an axis (two patches in a row: their roll).
    constexpr double kPairPrior = 10;
    constexpr int kMinAgreeing = 4;
    // What the fit assumes before any patch is seen (1/rad^2): yaw and pitch within about 2 degrees,
    // roll within 1. Matters only where the patches don't tell (roll, with patches all in one row).
    constexpr float kPriorYawPitch = 1 / (0.035f * 0.035f), kPriorRoll = 1 / (0.0175f * 0.0175f);

    // Bilinear weights for one fixed offset, the same for every texel of a patch.
    struct Bilinear {
        int ix = 0, iy = 0;
        float w00 = 1, w01 = 0, w10 = 0, w11 = 0;
    };

    Bilinear WeightsFor(float ox, float oy) {
        const float fx = std::floor(ox), fy = std::floor(oy);
        const float ax = ox - fx, ay = oy - fy;
        return {int(fx), int(fy), (1 - ax) * (1 - ay), ax * (1 - ay), (1 - ax) * ay, ax * ay};
    }

    inline float Sample(const float* p, int x, int y, const Bilinear& b) {
        const float* a = p + (y + b.iy) * kN + x + b.ix;
        return a[0] * b.w00 + a[1] * b.w01 + a[kN] * b.w10 + a[kN + 1] * b.w11;
    }

    // Smallest eigenvalue of the symmetric 2x2 [[a, b], [b, c]].
    float MinEigen(float a, float b, float c) {
        const float mean = 0.5f * (a + c), d = 0.5f * (a - c);
        return mean - std::sqrt(d * d + b * b);
    }

    // Solves the symmetric 3x3 system m x = v (Gaussian elimination); false if singular.
    bool Solve3(double m[3][3], double v[3], double x[3]) {
        for (int c = 0; c < 3; c++) {
            int p = c;
            for (int r = c + 1; r < 3; r++) {
                if (std::fabs(m[r][c]) > std::fabs(m[p][c])) {
                    p = r;
                }
            }

            if (std::fabs(m[p][c]) < 1e-12) {
                return false;
            }
            std::swap(m[c], m[p]);
            std::swap(v[c], v[p]);
            for (int r = c + 1; r < 3; r++) {
                const double f = m[r][c] / m[c][c];
                for (int k = c; k < 3; k++) {
                    m[r][k] -= f * m[c][k];
                }
                v[r] -= f * v[c];
            }
        }

        for (int c = 2; c >= 0; c--) {
            double s = v[c];
            for (int k = c + 1; k < 3; k++) {
                s -= m[c][k] * x[k];
            }
            x[c] = s / m[c][c];
        }

        return true;
    }

    // The best whole move of `b` against `a` (both kHalf square, means removed) near (gx, gy), refined
    // to a fraction by a parabola through the costs either side. False if none overlaps enough, or the
    // best lies at kCoarseReach: further than it reaches. The moves within 2 of the guess are tried, then
    // from the best so far downhill a step at a time (most moves are small: a few dozen tries, not 121).
    bool CoarseShift(const float* a, const float* b, float gx, float gy, float* sx, float* sy) {
        const int cx = int(std::lround(gx)), cy = int(std::lround(gy));
        constexpr int kSpan = 2 * kCoarseReach + 1;
        float cost[kSpan][kSpan];
        for (auto& row : cost) {
            for (float& c : row) {
                c = -1;  // not tried yet
            }
        }

        auto Cost = [&](int ox, int oy) {
            float& c = cost[oy + kCoarseReach][ox + kCoarseReach];
            if (c >= 0) {
                return c;
            }

            const int dx = cx + ox, dy = cy + oy;
            const int x0 = std::max(0, -dx), x1 = std::min(kHalf, kHalf - dx);
            const int y0 = std::max(0, -dy), y1 = std::min(kHalf, kHalf - dy);

            c = 1e30f;
            if ((x1 - x0) * (y1 - y0) >= kHalf * kHalf / 3) {
                double sum = 0;
                for (int y = y0; y < y1; y++) {
                    for (int x = x0; x < x1; x++) {
                        const float d = a[(y + dy) * kHalf + x + dx] - b[y * kHalf + x];
                        sum += d * d;
                    }
                }

                c = float(sum / ((x1 - x0) * (y1 - y0)));
            }

            return c;
        };

        float best = 1e30f;
        int bx = 0, by = 0;
        for (int oy = -2; oy <= 2; oy++) {
            for (int ox = -2; ox <= 2; ox++) {
                if (Cost(ox, oy) < best) {
                    best = Cost(ox, oy), bx = ox, by = oy;
                }
            }
        }

        for (bool moved = true; moved && best < 1e30f;) {
            moved = false;
            const int px = bx, py = by;
            for (int oy = std::max(-kCoarseReach, py - 1); oy <= std::min(kCoarseReach, py + 1); oy++) {
                for (int ox = std::max(-kCoarseReach, px - 1); ox <= std::min(kCoarseReach, px + 1); ox++) {
                    if (Cost(ox, oy) < best) {
                        best = Cost(ox, oy), bx = ox, by = oy, moved = true;
                    }
                }
            }
        }

        if (best >= 1e30f || std::abs(bx) == kCoarseReach || std::abs(by) == kCoarseReach) {
            return false;
        }

        // A parabola through the best cost and its two neighbours, per axis.
        auto Refine = [](float l, float m, float r) {
            const float den = l - 2 * m + r;
            return l < 1e30f && r < 1e30f && den > 0 ? std::clamp(0.5f * (l - r) / den, -0.5f, 0.5f) : 0.0f;
        };

        *sx = float(cx + bx) + Refine(Cost(bx - 1, by), best, Cost(bx + 1, by));
        *sy = float(cy + by) + Refine(Cost(bx, by - 1), best, Cost(bx, by + 1));
        return true;
    }

    // The two patches (prev: A, cur: B), contiguous, and their central-difference gradients.
    struct PatchPair {
        float A[kN * kN], B[kN * kN];
        float gAx[kN * kN] = {}, gAy[kN * kN] = {}, gBx[kN * kN] = {}, gBy[kN * kN] = {};
    };

    // Fills the pair from the two patches' texels (`stride` apart per row). Returns the weakest direction's
    // mean squared gradient of the cur patch.
    float LoadPair(const float* prev, const float* cur, int stride, PatchPair* p) {
        for (int y = 0; y < kN; y++) {
            for (int x = 0; x < kN; x++) {
                p->A[y * kN + x] = prev[y * stride + x], p->B[y * kN + x] = cur[y * stride + x];
            }
        }

        double bxx = 0, bxy = 0, byy = 0;
        for (int y = 1; y < kN - 1; y++) {
            for (int x = 1; x < kN - 1; x++) {
                const int t = y * kN + x;
                p->gAx[t] = 0.5f * (p->A[t + 1] - p->A[t - 1]),
                p->gAy[t] = 0.5f * (p->A[t + kN] - p->A[t - kN]);
                p->gBx[t] = 0.5f * (p->B[t + 1] - p->B[t - 1]),
                p->gBy[t] = 0.5f * (p->B[t + kN] - p->B[t - kN]);
                bxx += p->gBx[t] * p->gBx[t], bxy += p->gBx[t] * p->gBy[t], byy += p->gBy[t] * p->gBy[t];
            }
        }

        const float inner = float((kN - 2) * (kN - 2));
        return MinEigen(float(bxx / inner), float(bxy / inner), float(byy / inner));
    }

    // Coarse: the whole move at half resolution (2 x 2 averages, means removed), near the guess. The move
    // comes back in texels.
    bool CoarseMove(const PatchPair& p, float guessX, float guessY, float* sx, float* sy) {
        float a2[kHalf * kHalf], b2[kHalf * kHalf];
        double ma = 0, mb = 0;
        for (int y = 0; y < kHalf; y++) {
            for (int x = 0; x < kHalf; x++) {
                const int t = 2 * y * kN + 2 * x;
                a2[y * kHalf + x] = 0.25f * (p.A[t] + p.A[t + 1] + p.A[t + kN] + p.A[t + kN + 1]);
                b2[y * kHalf + x] = 0.25f * (p.B[t] + p.B[t + 1] + p.B[t + kN] + p.B[t + kN + 1]);
                ma += a2[y * kHalf + x], mb += b2[y * kHalf + x];
            }
        }

        for (int t = 0; t < kHalf * kHalf; t++) {
            a2[t] -= float(ma / (kHalf * kHalf)), b2[t] -= float(mb / (kHalf * kHalf));
        }

        float cx = 0, cy = 0;
        if (!CoarseShift(a2, b2, 0.5f * guessX, 0.5f * guessY, &cx, &cy)) {
            return false;
        }
        *sx = 2 * cx, *sy = 2 * cy;
        return true;
    }

    // The weighted sums of one fine step (in float: a few hundred terms of similar size, the hot loop of the
    // measuring): the normal matrix's (move x, move y, offset; residual derivative g_x, g_y, -1), the
    // residual's and the values'.
    struct StepSums {
        float xx = 0, xy = 0, yy = 0, x1 = 0, y1 = 0, w = 0, r2 = 0, a1 = 0, a2 = 0, rx = 0, ry = 0, r1 = 0;
    };

    StepSums
    SumStep(const float* res, const float* val, const float* gx, const float* gy, int count, float scale) {
        StepSums s;
        const float inv = scale > 0 ? 1 / scale : 0;
        for (int k = 0; k < count; k++) {
            const float r = res[k], q = r * inv, w = 1 / (1 + q * q);
            const float ex = gx[k], ey = gy[k], wex = w * ex, wey = w * ey, a = val[k];
            s.xx += wex * ex, s.xy += wex * ey, s.yy += wey * ey, s.x1 += wex, s.y1 += wey, s.w += w;
            s.rx += wex * r, s.ry += wey * r, s.r1 -= w * r;
            s.r2 += w * r * r, s.a1 += w * a, s.a2 += w * a * a;
        }

        return s;
    }

    // The gradients of both patches, each sampled half the move (hx0, hy0) away, at every second texel of
    // the part both patches have (a checkerboard, from mx and my in). Returns how many texels that is.
    int FineGradients(const PatchPair& p, float hx0, float hy0, int mx, int my, float* gx, float* gy) {
        int count = 0;
        const Bilinear wa = WeightsFor(hx0, hy0), wb = WeightsFor(-hx0, -hy0);
        for (int y = my; y < kN - my; y++) {
            for (int x = mx + ((y + mx) & 1); x < kN - mx; x += 2, count++) {
                gx[count] = 0.5f * (Sample(p.gAx, x, y, wa) + Sample(p.gBx, x, y, wb));
                gy[count] = 0.5f * (Sample(p.gAy, x, y, wa) + Sample(p.gBy, x, y, wb));
            }
        }

        return count;
    }

    // Fine: both patches sampled half the move apart, A(x + s/2) against B(x - s/2), so both get the
    // same interpolation blur (one-sided, the blur pulls a fit towards whole texels; see
    // ReprojectionError). Gauss-Newton on the move and a brightness offset, with the gradients taken
    // once at the start; from the second step on, texels that don't match count less (Cauchy weights
    // at kRobustScale times the residuals' median), so a part of the patch that moves on its own (the
    // scenery past a pillar's edge, the wheel) drops out instead of pulling the move towards its own.
    // Starts from the move in (moveX, moveY) and leaves the result there, with the last step's sums.
    bool FineMove(const PatchPair& p, float* moveX, float* moveY, int* count, StepSums* last) {
        float sx = *moveX, sy = *moveY;
        const float hx0 = 0.5f * sx, hy0 = 0.5f * sy;
        const int mx = int(std::ceil(std::fabs(hx0) + 1.5f)), my = int(std::ceil(std::fabs(hy0) + 1.5f));

        if (kN - 2 * mx < kN / 2 || kN - 2 * my < kN / 2) {
            return false;  // too little overlap left
        }

        float gx[kN * kN], gy[kN * kN], res[kN * kN], val[kN * kN], absRes[kN * kN];
        *count = FineGradients(p, hx0, hy0, mx, my, gx, gy);

        double bias = 0;
        float scale = 0;  // the Cauchy weights' scale (0: unweighted)

        for (int it = 0; it < kIterations; it++) {
            const Bilinear wa = WeightsFor(0.5f * sx, 0.5f * sy), wb = WeightsFor(-0.5f * sx, -0.5f * sy);
            int k = 0;
            for (int y = my; y < kN - my; y++) {
                for (int x = mx + ((y + mx) & 1); x < kN - mx; x += 2, k++) {
                    val[k] = Sample(p.A, x, y, wa);
                    res[k] = val[k] - Sample(p.B, x, y, wb) - float(bias);
                    if (it == 1) {
                        absRes[k] = std::fabs(res[k]);
                    }
                }
            }

            if (it == 1) {  // the scale from the residuals after the first (unweighted) step
                std::nth_element(absRes, absRes + *count / 2, absRes + *count);
                scale = std::max(kRobustScale * absRes[*count / 2], kNoiseFloor);
            }

            *last = SumStep(res, val, gx, gy, *count, scale);
            const StepSums& s = *last;
            double m[3][3] = {{s.xx, s.xy, -s.x1}, {s.xy, s.yy, -s.y1}, {-s.x1, -s.y1, s.w}};
            double v[3] = {-s.rx, -s.ry, -s.r1}, d[3];
            if (!Solve3(m, v, d)) {
                return false;
            }
            sx += float(d[0]), sy += float(d[1]), bias += d[2];
            if (std::fabs(0.5f * sx - hx0) > 1 || std::fabs(0.5f * sy - hy0) > 1) {
                return false;  // ran off
            }
            if (it > 0 && std::fabs(d[0]) < 0.005 && std::fabs(d[1]) < 0.005) {
                break;  // texels
            }
        }

        *moveX = sx, *moveY = sy;
        return true;
    }

    // How well the two match after the move (the texels that count), how much of the patch counts, and
    // how precisely the move is known: the move's part of the normal matrix (the offset taken out),
    // over the residual's variance. False when the match is too poor. Fills only `info`.
    bool CreditMove(const StepSums& s, int count, PatchShift* out) {
        const double hxx = s.xx, hxy = s.xy, hyy = s.yy, hx1 = s.x1, hy1 = s.y1, w1 = s.w, wr2 = s.r2;
        const double wa1 = s.a1, wa2 = s.a2;
        const double var = wa2 / w1 - (wa1 / w1) * (wa1 / w1);
        const double noise = std::max(wr2 / std::max(1.0, w1 - 3), double(kNoiseFloor) * kNoiseFloor);

        if (wr2 / w1 > kMaxMismatch * var || w1 < kMinCounted * count) {
            return false;
        }
        const double sxx = hxx - hx1 * hx1 / w1, sxy = hxy - hx1 * hy1 / w1, syy = hyy - hy1 * hy1 / w1;
        if (MinEigen(float(sxx / w1), float(sxy / w1), float(syy / w1)) < kMinTexture) {
            return false;
        }

        double ixx = sxx / noise, ixy = sxy / noise, iyy = syy / noise;
        const double cap = 2 / (double(kBestPrecision) * kBestPrecision);
        if (ixx + iyy > cap) {
            const double f = cap / (ixx + iyy);
            ixx *= f, ixy *= f, iyy *= f;
        }

        out->info[0] = float(ixx), out->info[1] = float(ixy), out->info[2] = float(iyy);
        return true;
    }

}

PatchShift MeasurePatchShift(const float* prev, const float* cur, int stride, float guessX, float guessY) {
    PatchShift out;
    PatchPair pair;

    // Too plain to measure: decided before any search (a dark cockpit costs next to nothing).
    if (LoadPair(prev, cur, stride, &pair) < kMinTexture) {
        return out;
    }

    float sx = 0, sy = 0;
    if (!CoarseMove(pair, guessX, guessY, &sx, &sy)) {
        return out;
    }

    int count = 0;
    StepSums last;
    if (!FineMove(pair, &sx, &sy, &count, &last)) {
        return out;
    }
    if (!CreditMove(last, count, &out)) {
        return out;
    }

    out.ok = true;
    out.dx = sx, out.dy = sy;
    return out;
}

namespace {

    // How far a patch's measured move is from what rotation w explains (texels, squared).
    double Misfit(const PatchMove& m, const double w[3]) {
        const double ex = m.dx
                          - (m.jacobian[0][0] * w[0] + m.jacobian[0][1] * w[1] + m.jacobian[0][2] * w[2]);

        const double ey = m.dy
                          - (m.jacobian[1][0] * w[0] + m.jacobian[1][1] * w[1] + m.jacobian[1][2] * w[2]);

        return ex * ex + ey * ey;
    }

    // The rotation x that two patches' four moves give: a yaw, pitch and roll, leaning a little towards none.
    bool PairRotation(const PatchMove& first, const PatchMove& second, double x[3]) {
        double m[3][3] = {{kPairPrior, 0, 0}, {0, kPairPrior, 0}, {0, 0, kPairPrior}}, v[3] = {};
        for (const PatchMove* p : {&first, &second}) {
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 3; c++) {
                    m[r][c] += p->jacobian[0][r] * p->jacobian[0][c] + p->jacobian[1][r] * p->jacobian[1][c];
                }

                v[r] += p->jacobian[0][r] * p->dx + p->jacobian[1][r] * p->dy;
            }
        }

        return Solve3(m, v, x);
    }

    // The rotation w that the patches `agree` marks fit best, each weighted by how precisely it is measured.
    bool WeightedRotation(const PatchMove* moves, const int* valid, const bool* agree, int n, double w[3]) {
        double a[3][3] = {{kPriorYawPitch, 0, 0}, {0, kPriorYawPitch, 0}, {0, 0, kPriorRoll}}, v[3] = {};
        for (int k = 0; k < n; k++) {
            if (!agree[k]) {
                continue;
            }
            const PatchMove& m = moves[valid[k]];
            const double I[2][2] = {{m.info[0], m.info[1]}, {m.info[1], m.info[2]}};
            for (int r = 0; r < 3; r++) {
                // (J^T I) row r
                const double jr0 = m.jacobian[0][r] * I[0][0] + m.jacobian[1][r] * I[1][0];
                const double jr1 = m.jacobian[0][r] * I[0][1] + m.jacobian[1][r] * I[1][1];
                for (int c = 0; c < 3; c++) {
                    a[r][c] += jr0 * m.jacobian[0][c] + jr1 * m.jacobian[1][c];
                }
                v[r] += jr0 * m.dx + jr1 * m.dy;
            }
        }

        return Solve3(a, v, w);
    }

}

bool FitCorrection(const PatchMove* moves, int count, Vec3* omega, int* agreeing) {
    *agreeing = 0;
    int valid[64], n = 0;
    for (int i = 0; i < count && i < 64; i++) {
        if (moves[i].ok) {
            valid[n++] = i;
        }
    }

    if (n < kMinAgreeing) {
        return false;
    }

    // The rotation most patches agree on: least median of squares over pairs of patches (each pair's
    // four moves give a yaw, pitch and roll), as the shake study found the cockpit in recordings.
    double w[3] = {}, best = 1e30;
    double misfit[64];

    for (int a = 0; a < n; a++) {
        for (int b = a + 1; b < n; b++) {
            double x[3];
            if (!PairRotation(moves[valid[a]], moves[valid[b]], x)) {
                continue;
            }
            for (int k = 0; k < n; k++) {
                misfit[k] = Misfit(moves[valid[k]], x);
            }
            std::nth_element(misfit, misfit + n / 2, misfit + n);
            if (misfit[n / 2] < best) {
                best = misfit[n / 2], std::copy(x, x + 3, w);
            }
        }
    }

    if (best >= 1e30) {
        return false;
    }

    // Then the patches that agree with it, weighted by how precisely each is measured, twice.
    const double limit = std::clamp(3 * std::sqrt(best), double(kAgreeFit), double(kAgreeMost));
    bool agree[64] = {};

    for (int pass = 0; pass < 2; pass++) {
        int in = 0;
        for (int k = 0; k < n; k++) {
            in += agree[k] = Misfit(moves[valid[k]], w) <= limit * limit;
        }
        if (in < kMinAgreeing || 2 * in < n) {
            return false;
        }
        if (!WeightedRotation(moves, valid, agree, n, w)) {
            return false;
        }
        *agreeing = in;
    }

    *omega = {float(w[0]), float(w[1]), float(w[2])};
    return true;
}
