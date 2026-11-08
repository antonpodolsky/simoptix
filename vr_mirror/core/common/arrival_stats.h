// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// ArrivalStats: the game's frame rate over 3-second windows (one frame more or less in a short
// window would show as 89 or 91), and the timing noise the arrival timing removed, over 12 seconds,
// for the periodic log lines. Logic only: times in, what to report out; the caller logs.
#pragma once

#include <cmath>

class ArrivalStats {
public:
    struct Report {
        bool window = false;  // a 3 s window closed: fps is its frame rate
        double fps = 0;
        bool lowRate = false;  // ...under 80 fps (reported at most every 10 s): frames over seconds
        int frames = 0;
        double seconds = 0;
        bool noise = false;  // 12 s of removed noise: its rms (ms)
        double noiseMs = 0;
    };

    // Timing starts again (after idle, a reconnect): a new window from the next pass.
    void Restart() { windowStart_ = 0, frames_ = 0; }
    // A frame arrived; `removed`: the noise the arrival timing took off its arrival (s), if it ran.
    void Frame() { frames_++; }
    void Removed(double removed) { noiseSq_ += removed * removed, noiseCount_++; }

    // Every poll pass, at source time `now`.
    Report Tick(double now) {
        Report r;
        if (now - windowStart_ < 3.0) {
            return r;
        }
        r.window = true;
        r.fps = windowStart_ > 0 ? frames_ / (now - windowStart_) : 0;
        if (windowStart_ > 0 && r.fps < 80 && now - lastLowRate_ >= 10) {
            lastLowRate_ = now;
            r.lowRate = true, r.frames = frames_, r.seconds = now - windowStart_;
        }

        windowStart_ = now;
        frames_ = 0;
        if (++windows_ % 4 == 0 && noiseCount_ > 0) {
            r.noise = true, r.noiseMs = std::sqrt(noiseSq_ / noiseCount_) * 1000;
            noiseSq_ = 0, noiseCount_ = 0;
        }

        return r;
    }

    // A new session: forget the noise collected too.
    void Reset() {
        Restart();
        noiseSq_ = 0, noiseCount_ = 0;
    }

private:
    double windowStart_ = 0, lastLowRate_ = 0, noiseSq_ = 0;
    int frames_ = 0, windows_ = 0, noiseCount_ = 0;
};
