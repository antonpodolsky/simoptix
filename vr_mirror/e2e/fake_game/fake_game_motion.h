// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Head motion for the fake headset: built-in synthetic movements. A motion spec is one or more parts
// joined with '+', e.g. "racing+heartbeat" (parts: still, heartbeat, head_tremor, racing, glances,
// edge_test).
#pragma once

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "common/rot.h"

namespace motion {

    constexpr float kPi = 3.14159265f;
    constexpr float kDeg = kPi / 180.0f;

    inline Quat FromYawPitchRoll(float yaw, float pitch, float roll) {
        Quat qy{0, std::sin(yaw / 2), 0, std::cos(yaw / 2)};
        Quat qx{std::sin(pitch / 2), 0, 0, std::cos(pitch / 2)};
        Quat qz{0, 0, std::sin(roll / 2), std::cos(roll / 2)};
        return Mul(Mul(qy, qx), qz);
    }

    struct Angles {
        float yaw = 0, pitch = 0, roll = 0;  // radians
    };

    // Deterministic smooth noise: sum of a few incommensurate sines.
    inline float SmoothNoise(double t, float seed) {
        return float(
            0.5 * std::sin(1.7 * t + seed) + 0.3 * std::sin(2.9 * t + 2.1 * seed)
            + 0.2 * std::sin(4.3 * t + 3.7 * seed)
        );
    }

    // Heartbeat: a sharp pulse once per beat (72 bpm) plus slow breathing.
    inline Angles Heartbeat(double t) {
        const double beat = std::fmod(t * 1.2, 1.0);
        const double pulse = beat < 0.15 ? std::sin(beat / 0.15 * kPi) : 0.0;  // ~125 ms bump
        const double breath = std::sin(2 * kPi * 0.25 * t);
        return {
            float(0.05 * pulse) * kDeg,
            float(0.12 * pulse + 0.25 * breath) * kDeg,
            float(0.03 * pulse) * kDeg
        };
    }

    // The headset worn, the head held still: physiological head tremor (6-12 Hz) with a fine shake
    // (13-19 Hz: tracking noise and the fast part of tremor), slow postural sway, and a slow lean
    // of +-0.6 deg towards a shoulder (every 8 s). Add Heartbeat for the pulse and breathing.
    // The runner's jitter measure sees little below 10 Hz (8 Hz: 10% of it, 12 Hz: 39%, 15 Hz:
    // 71%), so the fine shake is what it sees: 0.012 deg rms with the heartbeat, within the 0.007-
    // 0.017 deg measured on the real heads of vr_mirror\e2e\replay. 5-8 Hz alone measured 0.003 deg, under
    // head_still's own 0.005 deg limit: it passed with stabilization off.
    inline Angles HeadTremor(double t) {
        auto wave = [t](double hz, double phase) { return std::sin(2 * kPi * hz * t + phase); };
        const double yaw = 0.016 * wave(6.3, 0) + 0.012 * wave(8.7, 1.0) + 0.009 * wave(11.3, 2.5)
                           + 0.012 * wave(13.7, 0.4) + 0.009 * wave(17.9, 1.9);

        const double pitch = 0.013 * wave(7.1, 2.0) + 0.010 * wave(9.6, 0) + 0.007 * wave(12.2, 0.7)
                             + 0.010 * wave(15.3, 2.8) + 0.008 * wave(19.1, 0.2);

        const double lean = 0.6 * wave(0.125, 0);
        return {
            float(yaw + 0.3 * SmoothNoise(t, 0.3f)) * kDeg,
            float(pitch + 0.2 * SmoothNoise(t, 1.9f)) * kDeg,
            float(lean + 0.1 * SmoothNoise(t, 4.2f)) * kDeg
        };
    }

    // Everything that pushes the view towards the edge of the game image, one after the other in a
    // 12 s cycle: fast 70 deg turns right and left (~315 deg/s), fast 35 deg flicks up and down,
    // diagonal flicks, a 25 deg head tilt, a quick right-left-right shake (60 deg out and back, 0.25 s
    // each way, no pause), and a turn held 1 s at 50 deg with a tilt.
    inline Angles EdgeTest(double t) {
        // Each move: out to the target angles (deg) over `len` s from `start` s into the cycle, held
        // `hold` s, back to straight ahead over `len` s.
        struct Move {
            double start, len, hold, yaw, pitch, roll;
        };

        static const Move kMoves[] = {
            {0.3, 0.35, 0.35, 70, 4, 0},
            {1.4, 0.35, 0.35, -70, 4, 0},
            {2.5, 0.25, 0.25, 0, 35, 0},
            {3.3, 0.25, 0.25, 0, -35, 0},
            {4.1, 0.3, 0.3, 45, 30, 0},
            {5.0, 0.3, 0.3, -45, -30, 0},
            {5.9, 0.4, 0.4, 0, 0, 25},
            {7.2, 0.25, 0, 60, 0, 0},
            {7.7, 0.25, 0, -60, 0, 0},
            {8.2, 0.25, 0, 60, 0, 0},
            {9.2, 0.5, 1.0, 50, 10, 10}
        };

        const double p = std::fmod(t, 12.0);
        auto ease = [](double x) { return 0.5 - 0.5 * std::cos(kPi * std::min(1.0, std::max(0.0, x))); };
        Angles a;
        for (const Move& m : kMoves) {
            if (p < m.start || p > m.start + 2 * m.len + m.hold) {
                continue;
            }
            const double back = m.start + m.len + m.hold;
            const double x = p < back ? ease((p - m.start) / m.len) : 1 - ease((p - back) / m.len);
            a.yaw += float(m.yaw * x) * kDeg;
            a.pitch += float(m.pitch * x) * kDeg;
            a.roll += float(m.roll * x) * kDeg;
        }

        return a;
    }

    // Racing: frequent quick glances toward apexes and mirrors, with road vibration.
    inline Angles Racing(double t) {
        double glance = 0;
        const double cycle = 3.1;
        double p = std::fmod(t, cycle);
        if (p > 1.6 && p < 2.4) {
            glance = std::sin((p - 1.6) / 0.8 * kPi);
        }
        const float vib = float(0.08 * std::sin(2 * kPi * 23 * t) + 0.05 * std::sin(2 * kPi * 31 * t + 1));
        return {
            float(25 * std::sin(2 * kPi * 0.21 * t) + 35 * glance) * kDeg + vib * kDeg,
            float(3 * std::sin(2 * kPi * 0.37 * t)) * kDeg + vib * kDeg,
            float(2 * std::sin(2 * kPi * 0.19 * t)) * kDeg
        };
    }

    // Glances: Racing's sway and glances without its road vibration, as on a sim rig, where no car
    // motion reaches the head (add head_tremor and heartbeat for a real head's own small motion). Each
    // glance eases out and back (up to 137 deg/s), where Racing's starts at full speed at once: an
    // instant change of speed a real head can't make, which no pose prediction follows.
    inline Angles Glances(double t) {
        double glance = 0;
        const double p = std::fmod(t, 3.1);
        if (p > 1.6 && p < 2.4) {
            glance = 0.5 - 0.5 * std::cos((p - 1.6) / 0.8 * 2 * kPi);
        }
        return {
            float(25 * std::sin(2 * kPi * 0.21 * t) + 35 * glance) * kDeg,
            float(3 * std::sin(2 * kPi * 0.37 * t)) * kDeg,
            float(2 * std::sin(2 * kPi * 0.19 * t)) * kDeg
        };
    }

    class Motion {
    public:
        // Returns false with an error message for an unknown part.
        bool Parse(const std::string& spec, std::string* error) {
            size_t start = 0;
            while (start <= spec.size()) {
                const size_t plus = spec.find('+', start);
                std::string part = spec.substr(
                    start,
                    plus == std::string::npos ? std::string::npos : plus - start
                );

                if (part == "still" || part == "heartbeat" || part == "head_tremor" || part == "racing"
                    || part == "glances" || part == "edge_test") {
                    parts_.push_back(part);
                } else {
                    *error = "unknown motion part '" + part + "'";
                    return false;
                }

                if (plus == std::string::npos) {
                    break;
                }
                start = plus + 1;
            }

            return true;
        }

        // Head orientation `t` seconds after the motion started.
        Quat At(double t) const {
            Angles sum;
            for (const std::string& p : parts_) {
                Angles a = p == "heartbeat"     ? Heartbeat(t)
                           : p == "head_tremor" ? HeadTremor(t)
                           : p == "racing"      ? Racing(t)
                           : p == "glances"     ? Glances(t)
                           : p == "edge_test"   ? EdgeTest(t)
                                                : Angles{};

                sum.yaw += a.yaw;
                sum.pitch += a.pitch;
                sum.roll += a.roll;
            }

            return FromYawPitchRoll(sum.yaw, sum.pitch, sum.roll);
        }

    private:
        std::vector<std::string> parts_;
    };

}
