// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Replay of a real session for the fake runtime: the head poses, the runtime's per-slot pose
// predictions and display-slot timeline, and every game frame's arrival, as recorded by the OBS
// plugin's frame trace, plus the slot each frame was really rendered for (fitted to the recording).
// See vr_mirror/e2e/replay/*.csv for the format. Times are seconds from the start of the file; the fake
// runtime plays it from its own start. Knows nothing about D3D or the PVR interface.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "common/rot.h"

namespace replay {

    struct Pose {
        double t;
        Quat q;
    };

    struct Prediction {
        double asked, slot;  // asked at `asked`, for display slot `slot`
        Quat q;
    };

    struct Frame {
        long long serial;
        double arrival, slot;  // arrival, and the display slot the real tool assigned it to
        int lag;               // rendered for slot + lag display periods
    };

    class Replay {
    public:
        bool Load(const std::string& path, std::string* error) {
            FILE* f = nullptr;
            if (fopen_s(&f, path.c_str(), "r") != 0 || !f) {
                *error = "cannot open replay " + path;
                return false;
            }

            char line[256];
            while (fgets(line, sizeof(line), f)) {
                Quat q;
                if (line[0] == 'P') {
                    Pose p;
                    if (sscanf_s(line, "P,%lf,%f,%f,%f,%f", &p.t, &q.x, &q.y, &q.z, &q.w) == 5) {
                        poses_.push_back({p.t, q});
                    }
                } else if (line[0] == 'S') {
                    Prediction p;
                    if (sscanf_s(line, "S,%lf,%lf,%f,%f,%f,%f", &p.asked, &p.slot, &q.x, &q.y, &q.z, &q.w)
                        == 6) {
                        p.q = q;
                        predictions_.push_back(p);
                    }
                } else if (line[0] == 'F') {
                    Frame fr;
                    if (sscanf_s(line, "F,%lld,%lf,%lf,%d", &fr.serial, &fr.arrival, &fr.slot, &fr.lag)
                        == 4) {
                        frames_.push_back(fr);
                    }
                }
            }

            fclose(f);
            if (poses_.size() < 2 || predictions_.empty() || frames_.empty()) {
                *error = "replay " + path + " has no poses, predictions or frames";
                return false;
            }

            return true;
        }

        // Recorded head pose at t (interpolated; held before the first and after the last sample).
        Quat HeadAt(double t) const {
            auto it = std::lower_bound(poses_.begin(), poses_.end(), t, [](const Pose& p, double v) {
                return p.t < v;
            });

            if (it == poses_.begin()) {
                return it->q;
            }
            if (it == poses_.end()) {
                return poses_.back().q;
            }
            const Pose& b = *it;
            const Pose& a = *(it - 1);
            return Normalize(Slerp(a.q, b.q, float((t - a.t) / std::max(1e-9, b.t - a.t))));
        }

        // The runtime's prediction for display slot `slot`, if it was recorded (asked by `now`).
        bool PredictionFor(double slot, double now, double period, Quat* q) const {
            const Prediction* p = Find(slot, period);
            if (!p || p->asked > now + 0.002) {
                return false;
            }
            *q = p->q;
            return true;
        }

        // ...regardless of when it was asked (the game's view of its frame).
        bool PredictionFor(double slot, double period, Quat* q) const {
            const Prediction* p = Find(slot, period);
            if (p) {
                *q = p->q;
            }
            return p != nullptr;
        }

        // The runtime's next display slot at time t: the latest slot announced by then.
        double SlotAt(double t, double period) const {
            auto it = std::upper_bound(
                predictions_.begin(),
                predictions_.end(),
                t,
                [](double v, const Prediction& p) { return v < p.asked; }
            );

            if (it == predictions_.begin()) {
                return predictions_.front().slot - period;
            }
            return (it - 1)->slot;
        }

        const std::vector<Frame>& Frames() const { return frames_; }
        // The recorded rows themselves, in time order (vr_mirror\e2e\stage_replay feeds them to core's pose
        // history).
        const std::vector<Pose>& Poses() const { return poses_; }
        const std::vector<Prediction>& Predictions() const { return predictions_; }
        double Duration() const { return poses_.back().t; }

    private:
        const Prediction* Find(double slot, double period) const {
            auto it = std::lower_bound(
                predictions_.begin(),
                predictions_.end(),
                slot - 0.5 * period,
                [](const Prediction& p, double v) { return p.slot < v; }
            );

            if (it == predictions_.end() || std::fabs(it->slot - slot) > 0.25 * period) {
                return nullptr;
            }
            return &*it;
        }

        std::vector<Pose> poses_;
        std::vector<Prediction> predictions_;  // in order of slot (and of asking)
        std::vector<Frame> frames_;
    };

}
