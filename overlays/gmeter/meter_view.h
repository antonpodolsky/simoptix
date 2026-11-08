// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Meter view: what the G-meter's look (meter_chrono.h) draws in one frame, in plain values: the
// smoothed G, its recent trail, the highest values, the pedals. The source (gmeter_source.cpp) fills it from
// telemetry; the look only reads it.
//
// Knows nothing about OBS sources, settings storage, telemetry reading or drawing.
#pragma once

#include <cstddef>

struct PedalState {
    float brake = 0, throttle = 0;  // the driver's pedals, 0-1
    bool absActive = false, tcActive = false;
};

struct TrailPoint {
    double t;    // performance-counter seconds
    float x, y;  // G: x right, y forward (up on screen)
};

// The tail's recent points: a fixed ring, so nothing is allocated per frame (10 s at 100 updates a
// second; the tail is at most 3 s).
class Trail {
public:
    size_t size() const { return n_; }
    bool empty() const { return n_ == 0; }
    const TrailPoint& operator[](size_t i) const { return p_[(head_ + kCap - n_ + i) % kCap]; }
    const TrailPoint& front() const { return (*this)[0]; }
    void push_back(const TrailPoint& x) {
        p_[head_] = x;
        head_ = (head_ + 1) % kCap;
        n_ = n_ < kCap ? n_ + 1 : kCap;
    }

    void pop_front() { n_ = n_ ? n_ - 1 : 0; }
    void clear() { n_ = 0; }

private:
    static constexpr size_t kCap = 1024;
    TrailPoint p_[kCap];
    size_t head_ = 0, n_ = 0;
};

struct MeterView {
    float size = 400;      // the width and height to draw at, in pixels (the meter's size on the canvas)
    double now = 0;        // performance-counter seconds
    bool live = false;     // telemetry is coming in
    float gx = 0, gy = 0;  // the smoothed G (as TrailPoint)
    const Trail* trail = nullptr;
    float trailS = 1;                                              // the tail's length, seconds
    float maxForward = 0, maxBack = 0, maxRight = 0, maxLeft = 0;  // highest G reached each way
    PedalState pedals;
};
