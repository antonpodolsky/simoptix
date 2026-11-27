// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// RecordedSound: a recording's sound that fake_openxr plays for auto sync's e2e test, through Windows'
// default output device, so OBS captures it as a game's sound. Knows nothing about OpenXR or the frames.
#pragma once

#include <windows.h>

#include <atomic>
#include <limits>
#include <string>
#include <thread>
#include <vector>

// A recording (a file of mono 32-bit float samples), played so its sample at `t` seconds is heard at `start`
// (Begin) + `lag` + `t`, on the performance counter, and after a restart (Restart) from its start again. It
// learns when each sample plays from the device's clock (IAudioClock), so the timing is the device's, and
// plays at the device's own rate.
class RecordedSound {
public:
    RecordedSound() = default;
    RecordedSound(const RecordedSound&) = delete;
    RecordedSound& operator=(const RecordedSound&) = delete;
    ~RecordedSound() { Stop(); }

    // The recording in `path`, `rate` samples a second; false if it can't be read.
    bool Load(const std::string& path, double rate);
    // Silent until Begin, then `lag` (s) late. What goes wrong on the sound's own thread is printed there.
    void Start(double lag);
    // The recording's first sample at `start` (performance-counter seconds, before the lag). Any thread.
    void Begin(double start) { start_ = start; }
    // Silent from `stop` on, then the recording's first sample once more at `again` (performance-counter
    // seconds, before the lag). Any thread.
    void Restart(double stop, double again);
    void Stop();

private:
    void Run();

    std::vector<float> samples_;
    double rate_ = 0, lag_ = 0;
    std::atomic<double> start_{std::numeric_limits<double>::infinity()};
    std::atomic<double> stop_{std::numeric_limits<double>::infinity()}, again_{0};  // 0: no second play
    std::atomic<bool> running_{false};
    std::thread thread_;
};
