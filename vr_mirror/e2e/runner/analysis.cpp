// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "analysis.h"

// What the tool meant to show at each of its frames (after the first second), and where the
// eye pointed at those moments.
void IntendedSeries(const ToolTrace& tt, const FakeLog& log, double delay, Series* intended, Series* eye) {
    if (tt.presented.empty()) {
        return;
    }
    for (size_t i = 0; i < tt.presented.size(); i++) {
        if (tt.presented[i] < tt.presented.front() + 1.0) {
            continue;
        }
        const double t = tt.presented[i];
        intended->t.push_back(t);
        intended->dir.push_back(Forward(Mul(tt.view[i], log.eyeCant)));
        eye->t.push_back(t);
        eye->dir.push_back(Forward(log.EyeAt(t - delay)));
    }
}

Matched MatchCaptures(
    const std::vector<ViewSample>& caps,
    const FakeLog& log,
    const ToolTrace& tt,
    double delay,
    double* validPct
) {
    Matched m;
    int valid = 0, last = -1;

    for (const auto& c : caps) {
        if (!c.valid) {
            continue;
        }
        valid++;
        const int newest = tt.PresentBefore(c.t);

        int best = -1;
        double bestAngle = 1e9;
        for (int p = newest; p >= 0 && p >= newest - 4; p--) {
            const double a = AngleDeg(c.forward, Forward(Mul(tt.view[p], log.eyeCant)));
            if (a < bestAngle) {
                bestAngle = a;
                best = p;
            }
        }

        if (best < 0 || best <= last) {
            continue;  // each frame once, in order
        }
        last = best;
        const double t = tt.presented[best];
        if (t < tt.presented.front() + 1.0) {
            continue;
        }
        m.seen.t.push_back(t);
        m.seen.dir.push_back(c.forward);
        m.intended.t.push_back(t);
        m.intended.dir.push_back(Forward(Mul(tt.view[best], log.eyeCant)));
        m.eye.t.push_back(t);
        m.eye.dir.push_back(Forward(log.EyeAt(t - delay)));
        m.roll.push_back(RollDeg(c.right));
    }

    *validPct = caps.empty() ? 0 : 100.0 * valid / caps.size();
    return m;
}

// Like a 60 fps OBS recording: each tick shows the newest captured frame. Returns the share
// of ticks that repeat the previous image while the head is moving (visible as stutter).
double ObsRepeatPct(const std::vector<ViewSample>& caps, const FakeLog& log) {
    std::vector<const ViewSample*> valid;
    for (const auto& c : caps) {
        if (c.valid) {
            valid.push_back(&c);
        }
    }

    if (valid.size() < 10) {
        return 0;
    }

    size_t j = 0;
    const ViewSample* last = nullptr;
    int moving = 0, repeats = 0;

    for (double t = valid.front()->t + 0.1; t < valid.back()->t; t += 1.0 / 60) {
        const ViewSample* cur = last;
        while (j < valid.size() && valid[j]->t <= t) {
            cur = valid[j++];
        }
        if (!cur) {
            continue;
        }
        double headSpeed = AngleDeg(Forward(log.HeadAt(t - 1.0 / 60)), Forward(log.HeadAt(t))) * 60;
        if (last && headSpeed > 10) {
            moving++;
            repeats += cur == last;
        }

        last = cur;
    }

    return moving ? 100.0 * repeats / moving : 0;
}

// Rate and longest gap of the tool's presents, skipping the first second (startup).
void ToolPresentStats(const ToolTrace& tt, double* fps, double* maxGapMs) {
    *fps = *maxGapMs = 0;
    const auto& p = tt.presented;
    if (p.size() < 10) {
        return;
    }
    size_t start = 0;
    while (start < p.size() && p[start] < p.front() + 1.0) {
        start++;
    }
    for (size_t i = start + 1; i < p.size(); i++) {
        *maxGapMs = std::max(*maxGapMs, (p[i] - p[i - 1]) * 1000);
    }
    *fps = (p.size() - 1 - start) / (p.back() - p[start]);
}

// How unevenly the tool's presents are spaced (rms of each step's distance from a whole number
// of 90 Hz frames), skipping the first second. OBS grabs the newest present on its own clock;
// presents that wobble around its grab time make it repeat and skip frames.
double PresentJitterMs(const ToolTrace& tt) {
    const auto& p = tt.presented;
    if (p.size() < 10) {
        return 0;
    }

    const double period = 1.0 / 90;
    size_t start = 0;
    while (start < p.size() && p[start] < p.front() + 1.0) {
        start++;
    }
    double sumSq = 0;
    int n = 0;

    for (size_t i = start + 1; i < p.size(); i++) {
        const double step = p[i] - p[i - 1];
        const double off = step - std::max(1.0, std::round(step / period)) * period;
        sumSq += off * off;
        n++;
    }

    return n ? std::sqrt(sumSq / n) * 1000 : 0;
}

void PresentDelayStats(const ToolTrace& tt, double* medianMs, double* spreadMs) {
    *medianMs = *spreadMs = 0;
    const auto& p = tt.presented;
    if (p.size() < 10) {
        return;
    }
    std::vector<double> d;
    for (size_t i = 0; i < p.size(); i++) {
        if (p[i] >= p.front() + 1.0) {
            d.push_back((p[i] - tt.arrival[i]) * 1000);
        }
    }

    if (d.size() < 10) {
        return;
    }
    std::sort(d.begin(), d.end());
    *medianMs = d[d.size() / 2];
    *spreadMs = d[d.size() * 99 / 100] - d[d.size() / 100];
}

// Game frames (after the first second) that the tool never presented: no tool frame saw an
// arrival within 6 ms after the game delivered it.
int MissedGameFrames(const FakeLog& log, const ToolTrace& tt, int* considered) {
    int missed = 0;
    *considered = 0;
    if (log.arrivals.empty() || tt.arrival.empty()) {
        return 0;
    }
    double from = tt.arrival.front() + 1.0, to = tt.arrival.back() - 0.1;
    for (double a : log.arrivals) {
        if (a < from || a > to) {
            continue;
        }
        (*considered)++;
        auto it = std::lower_bound(tt.arrival.begin(), tt.arrival.end(), a - 0.001);
        if (it == tt.arrival.end() || *it > a + 0.006) {
            missed++;
        }
    }

    return missed;
}

int MissedAtStart(const FakeLog& log, const ToolTrace& tt) {
    for (size_t k = 0; k < log.arrivals.size(); k++) {
        const double a = log.arrivals[k];
        auto it = std::lower_bound(tt.arrival.begin(), tt.arrival.end(), a - 0.001);
        if (it != tt.arrival.end() && *it <= a + 0.006) {
            return int(k);
        }
    }

    return int(log.arrivals.size());
}

void RepeatSkipPct(const FakeLog& log, const ToolTrace& tt, double* repeatPct, double* skipPct) {
    *repeatPct = *skipPct = 0;
    if (tt.presented.size() < 10) {
        return;
    }
    int frames = 0, repeats = 0, skips = 0;
    for (size_t i = 1; i < tt.presented.size(); i++) {
        if (tt.presented[i] < tt.presented.front() + 1.0) {
            continue;
        }
        frames++;
        if (tt.serial[i] == tt.serial[i - 1]) {
            repeats++;
            continue;
        }

        // A game frame delivered between the two shown ones (arrival matched within 3 ms) was skipped.
        auto it = std::upper_bound(log.arrivals.begin(), log.arrivals.end(), tt.arrival[i - 1] + 0.003);
        skips += it != log.arrivals.end() && *it < tt.arrival[i] - 0.003;
    }

    if (frames) {
        *repeatPct = 100.0 * repeats / frames, *skipPct = 100.0 * skips / frames;
    }
}

// Frames the tool re-projected from a different head pose than the fake game rendered them with
// (matched by arrival time; more than 0.02 deg apart). Exact in replays, where the fake renders
// with the runtime's recorded predictions, which are also what the tool looks up.
namespace {

    // Angle of a rotation in degrees, robust for nearly no rotation: 2 atan2(|v|, |w|).
    double RotationDeg(const Quat& d) {
        return 2
               * std::atan2(
                   std::sqrt(double(d.x) * d.x + double(d.y) * d.y + double(d.z) * d.z),
                   std::fabs(double(d.w))
               )
               / kDeg;
    }

}

std::vector<Quat> PoseErrors(const FakeLog& log, const ToolTrace& tt) {
    std::vector<Quat> errors;
    for (size_t i = 0; i < tt.arrival.size(); i++) {
        if (i > 0 && tt.serial[i] == tt.serial[i - 1]) {
            continue;  // a repeat: counted at its first tick
        }
        auto it = std::lower_bound(log.arrivals.begin(), log.arrivals.end(), tt.arrival[i] - 0.003);
        if (it == log.arrivals.end() || *it > tt.arrival[i] + 0.003) {
            continue;
        }
        const Quat& a = tt.raw[i];
        const Quat& b = log.framePoses[it - log.arrivals.begin()];
        errors.push_back(Mul(Conj(Normalize(a)), Normalize(b)));
    }

    return errors;
}

std::vector<double> PoseErrorsDeg(const FakeLog& log, const ToolTrace& tt) {
    std::vector<double> degs;
    for (const Quat& d : PoseErrors(log, tt)) {
        degs.push_back(RotationDeg(d));
    }
    return degs;
}

std::vector<double> PoseJumpsDeg(const FakeLog& log, const ToolTrace& tt) {
    const std::vector<Quat> errors = PoseErrors(log, tt);
    std::vector<double> jumps;
    for (size_t i = 1; i < errors.size(); i++) {
        jumps.push_back(RotationDeg(Mul(Conj(errors[i - 1]), errors[i])));
    }

    return jumps;
}

double LargestMeanDeg(const std::vector<Vec3>& v, int window) {
    if (v.empty()) {
        return 0;
    }
    window = std::max(1, std::min(window, int(v.size())));
    double sx = 0, sy = 0, sz = 0, largest = 0;
    for (size_t i = 0; i < v.size(); i++) {
        sx += v[i].x, sy += v[i].y, sz += v[i].z;
        if (i >= size_t(window)) {
            sx -= v[i - window].x, sy -= v[i - window].y, sz -= v[i - window].z;
        }
        if (i + 1 >= size_t(window)) {
            largest = std::max(largest, std::sqrt(sx * sx + sy * sy + sz * sz) / window);
        }
    }

    return largest;
}

std::vector<Vec3> PoseErrorVectorsDeg(const FakeLog& log, const ToolTrace& tt) {
    std::vector<Vec3> out;
    for (const Quat& d : PoseErrors(log, tt)) {
        const Vec3 r = RotationVector(d);
        out.push_back({float(r.x / kDeg), float(r.y / kDeg), float(r.z / kDeg)});
    }

    return out;
}

int WrongPoseFrames(const FakeLog& log, const ToolTrace& tt, int* considered) {
    const std::vector<double> errors = PoseErrorsDeg(log, tt);
    *considered = int(errors.size());
    return int(std::count_if(errors.begin(), errors.end(), [](double e) { return e > 0.02; }));
}

Series RecordedSeries(const ToolTrace& tt, const Quat& eyeCant, double fps, double phase) {
    Series s;
    const auto& p = tt.presented;
    if (p.size() < 10) {
        return s;
    }
    for (double t = p.front() + 1.0 + phase / fps; t < p.back(); t += 1 / fps) {
        const int i = tt.PresentBefore(t);
        if (i < 0) {
            continue;
        }
        s.t.push_back(t);
        s.dir.push_back(Forward(Mul(tt.view[i], eyeCant)));
    }

    return s;
}

double RecordedJitterDeg(const ToolTrace& tt, const Quat& eyeCant, double fps) {
    const int kPhases = 6;
    double sum = 0;
    for (int k = 0; k < kPhases; k++) {
        sum += JitterDeg(RecordedSeries(tt, eyeCant, fps, double(k) / kPhases));
    }
    return sum / kPhases;
}

// Jitter: what's left after fitting a smooth curve (a local quadratic over ±50 ms) around each
// sample (rms, degrees). A deliberate head turn, however curved, fits that and counts as zero;
// tremor, vibration and frame-to-frame shake (faster than ~5 Hz) don't, and count fully.
double JitterDeg(const Series& s) {
    const size_t n = s.t.size();
    if (n < 20) {
        return 0;
    }
    std::vector<double> yaw(n), pitch(n);
    for (size_t i = 0; i < n; i++) {
        yaw[i] = YawDeg(s.dir[i]);
        pitch[i] = PitchDeg(s.dir[i]);
        if (i && yaw[i] - yaw[i - 1] > 180) {
            yaw[i] -= 360;
        }
        if (i && yaw[i] - yaw[i - 1] < -180) {
            yaw[i] += 360;
        }
    }

    const double kHalfWindow = 0.05;
    double sum = 0;
    int count = 0;
    size_t lo = 0, hi = 0;
    for (size_t i = 0; i < n; i++) {
        if (s.t[i] - s.t.front() < kHalfWindow || s.t.back() - s.t[i] < kHalfWindow) {
            continue;
        }
        while (s.t[lo] < s.t[i] - kHalfWindow) {
            lo++;
        }
        while (hi + 1 < n && s.t[hi + 1] <= s.t[i] + kHalfWindow) {
            hi++;
        }
        if (hi - lo < 4) {
            continue;
        }

        // Least-squares fit of a + b*u + c*u^2 (u = time from sample i) to yaw and to pitch;
        // the fitted value at u = 0 is a.
        double S[5] = {0, 0, 0, 0, 0}, Ty[3] = {0, 0, 0}, Tp[3] = {0, 0, 0};
        for (size_t k = lo; k <= hi; k++) {
            double u = s.t[k] - s.t[i], p = 1;
            for (int e = 0; e < 5; e++, p *= u) {
                S[e] += p;
            }
            Ty[0] += yaw[k], Ty[1] += yaw[k] * u, Ty[2] += yaw[k] * u * u;
            Tp[0] += pitch[k], Tp[1] += pitch[k] * u, Tp[2] += pitch[k] * u * u;
        }

        // Solve [S0 S1 S2; S1 S2 S3; S2 S3 S4] x = T for x[0] by Cramer's rule.
        auto det3 =
            [](double a, double b, double c, double d, double e, double f, double g, double h, double k) {
                return a * (e * k - f * h) - b * (d * k - f * g) + c * (d * h - e * g);
            };

        const double det = det3(S[0], S[1], S[2], S[1], S[2], S[3], S[2], S[3], S[4]);
        if (std::fabs(det) < 1e-30) {
            continue;
        }
        const double ay = det3(Ty[0], S[1], S[2], Ty[1], S[2], S[3], Ty[2], S[3], S[4]) / det;
        const double ap = det3(Tp[0], S[1], S[2], Tp[1], S[2], S[3], Tp[2], S[3], S[4]) / det;
        double dy = (yaw[i] - ay) * std::cos(pitch[i] * kDeg), dp = pitch[i] - ap;
        sum += dy * dy + dp * dp;
        count++;
    }

    return count ? std::sqrt(sum / count) : 0;
}

namespace {

    std::vector<double> Speeds(const Series& s) {
        const size_t n = s.t.size();
        std::vector<double> v(n, 0);
        for (size_t i = 1; i < n; i++) {
            v[i] = AngleDeg(s.dir[i - 1], s.dir[i]) / std::max(1e-4, s.t[i] - s.t[i - 1]);
        }

        std::vector<double> sm(n, 0);  // 3-sample smoothing against frame-pacing noise
        for (size_t i = 1; i + 1 < n; i++) {
            sm[i] = (v[i - 1] + v[i] + v[i + 1]) / 3;
        }
        return sm;
    }

    // The direction moved over the two samples up to i (unnormalized; zero at the start).
    Vec3 Motion(const Series& s, size_t i) {
        if (i < 2) {
            return {};
        }
        const Vec3& a = s.dir[i - 2];
        const Vec3& b = s.dir[i];
        return {b.x - a.x, b.y - a.y, b.z - a.z};
    }

}

// During each head turn (eye faster than 40 deg/s) and 1 s after it, count places where the
// view's speed drops below half of the peak before it and then rises again to more than twice
// that low point (and by at least 20 deg/s) while still moving the same way: the view stalling
// and then catching up. A dip where the view reverses is the head turning around (edge_test's
// shake reverses with no pause; counted, it read as 3 stalls, one per turnaround).
int CountStalls(const Series& view, const Series& eye) {
    std::vector<double> vs = Speeds(view), hs = Speeds(eye);
    int stalls = 0;
    size_t i = 0;
    while (i < hs.size()) {
        if (hs[i] < 40) {
            i++;
            continue;
        }

        const size_t start = i;
        while (i < hs.size() && hs[i] >= 40) {
            i++;
        }

        // Watch the view settle for up to 1 s, but stop where the next head movement begins:
        // the view speeding up for a new turn is not a catch-up.
        double until = eye.t[std::min(i, eye.t.size() - 1)] + 1.0;
        for (size_t n = i; n < hs.size() && eye.t[n] < until; n++) {
            if (hs[n] >= 20) {
                until = eye.t[n];
                break;
            }
        }

        double peak = 0, dip = 0;
        bool inDip = false;
        Vec3 into;  // the view's motion as it entered the dip
        size_t k = start;

        for (; k < vs.size() && view.t[k] <= until; k++) {
            if (!inDip) {
                peak = std::max(peak, vs[k]);
                if (peak > 30 && vs[k] < 0.5 * peak) {
                    inDip = true;
                    dip = vs[k];
                    into = Motion(view, k);
                }
            } else {
                dip = std::min(dip, vs[k]);
                if (vs[k] > 2 * dip && vs[k] > dip + 20) {
                    // Speeding up the same way is a catch-up; the other way, the view turned
                    // around with the head (a quick shake), which is not a stall.
                    const Vec3 out = Motion(view, k);
                    stalls += into.x * out.x + into.y * out.y + into.z * out.z > 0;
                    inDip = false;
                    peak = vs[k];
                }
            }
        }

        i = std::max(i, k);
    }

    return stalls;
}

// Stabilization off: the view should match the eye up to a small constant delay.
double TrackErrorDeg(const Series& seen, const FakeLog& log, double* bestDelayMs) {
    double best = 1e9;
    for (int ms = -40; ms <= 40; ms += 2) {
        double sum = 0;
        for (size_t i = 0; i < seen.t.size(); i++) {
            sum += AngleDeg(seen.dir[i], Forward(log.EyeAt(seen.t[i] - ms / 1000.0)));
        }

        const double mean = sum / seen.t.size();
        if (mean < best) {
            best = mean;
            *bestDelayMs = ms;
        }
    }

    return best;
}

void Check(Result& r, const char* what, double value, double limit, const char* unit, bool lessIsBetter) {
    char buf[256];
    if (limit < 0) {
        snprintf(buf, sizeof(buf), "  %-26s %9.3f %s", what, value, unit);
    } else {
        const bool ok = lessIsBetter ? value <= limit : value >= limit;
        if (!ok) {
            r.pass = false;
        }
        snprintf(
            buf,
            sizeof(buf),
            "  %-26s %9.3f %s  (limit %s %.3f)  %s",
            what,
            value,
            unit,
            lessIsBetter ? "<=" : ">=",
            limit,
            ok ? "ok" : "FAIL"
        );
    }

    r.lines.push_back(buf);
    r.values[what] = value;
}

// One row per captured tool frame: what was seen, what the tool intended, where the eye was.
void WriteTrackCsv(const fs::path& path, const Matched& m) {
    std::ofstream f(path);
    f << "t,seen_yaw,seen_pitch,intended_yaw,intended_pitch,eye_yaw,eye_pitch,seen_roll\n";
    f << std::fixed;
    f.precision(5);
    for (size_t i = 0; i < m.seen.t.size(); i++) {
        f << m.seen.t[i] - m.seen.t[0] << "," << YawDeg(m.seen.dir[i]) << "," << PitchDeg(m.seen.dir[i])
          << "," << YawDeg(m.intended.dir[i]) << "," << PitchDeg(m.intended.dir[i]) << ","
          << YawDeg(m.eye.dir[i]) << "," << PitchDeg(m.eye.dir[i]) << "," << m.roll[i] << "\n";
    }
}
