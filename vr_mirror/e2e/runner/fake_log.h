// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The fake runtime's log (fake.log): the true head motion and every game frame it rendered, with
// the pose it rendered it with. Data only.
#pragma once

#include "common.h"

struct TimedQuat {
    double t;
    Quat q;
};

struct FakeLog {
    std::vector<TimedQuat> poses;  // true head at ~1 kHz
    std::vector<double> arrivals;  // game frame arrival times
    std::vector<double> targets;   // ...and the display slot each was drawn for
    std::vector<Quat> framePoses;  // the head pose each game frame was rendered with
    Quat eyeCant;                  // the image's eye relative to the head
    // No game yet (game_start_s): calls the tool made into the runtime meanwhile, over how long
    // (seconds); -1 when the scenario has no such wait.
    long long idleCalls = -1;
    double idleSeconds = 0;

    bool Load(const fs::path& path) {
        std::ifstream f(path);
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("eyecant,", 0) == 0) {
                sscanf_s(line.c_str(), "eyecant,%f,%f,%f,%f", &eyeCant.x, &eyeCant.y, &eyeCant.z, &eyeCant.w);
                eyeCant = Normalize(eyeCant);  // logged with 4 decimals: not exactly unit length
            } else if (line.rfind("pose,", 0) == 0) {
                TimedQuat p;
                if (sscanf_s(line.c_str(), "pose,%lf,%f,%f,%f,%f", &p.t, &p.q.x, &p.q.y, &p.q.z, &p.q.w)
                    == 5) {
                    poses.push_back(p);
                }
            } else if (line.rfind("idle_calls,", 0) == 0) {
                sscanf_s(line.c_str(), "idle_calls,%lld,%lf", &idleCalls, &idleSeconds);
            } else if (line.rfind("frame,", 0) == 0) {
                long long k;
                double begin, target, arrival;
                Quat q;
                if (sscanf_s(
                        line.c_str(),
                        "frame,%lld,%lf,%lf,%lf,%f,%f,%f,%f",
                        &k,
                        &begin,
                        &target,
                        &arrival,
                        &q.x,
                        &q.y,
                        &q.z,
                        &q.w
                    )
                    == 8) {
                    arrivals.push_back(arrival);
                    targets.push_back(target);
                    framePoses.push_back(q);
                }
            }
        }

        return poses.size() > 100;
    }

    Quat HeadAt(double t) const {
        auto it = std::lower_bound(poses.begin(), poses.end(), t, [](const TimedQuat& p, double v) {
            return p.t < v;
        });

        if (it == poses.begin()) {
            return poses.front().q;
        }
        if (it == poses.end()) {
            return poses.back().q;
        }
        const TimedQuat& b = *it;
        const TimedQuat& a = *(it - 1);
        return Normalize(Slerp(a.q, b.q, float((t - a.t) / std::max(1e-9, b.t - a.t))));
    }

    // Where the output center should point: the tool centers the output on the eye's axis.
    Quat EyeAt(double t) const { return Normalize(Mul(HeadAt(t), eyeCant)); }
};
