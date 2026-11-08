// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The inputs stage_replay runs the pipeline on: a replay of a real session (vr_mirror\e2e\replay\*.csv: head
// poses, the runtime's per-slot predictions, game frames with the lag they were really drawn with) or
// a synthetic motion spec (vr_mirror\e2e\fake_game\fake_game_motion.h, perfect predictions), and ReplayPoses,
// a PoseSource over one: it feeds core's PoseHistory in time order up to the moment a host draws, as
// PimaxSource's poll thread does live, so later poses and predictions are not seen early. Knows nothing about
// the pipeline, D3D or the fake runtime.
#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "common/pose_history.h"
#include "common/pose_source.h"
#include "common/rot.h"
#include "fake_game_motion.h"
#include "fake_pvr_replay.h"

namespace stage_replay {

    constexpr double kPeriod = 1.0 / 90;  // the fake's and the Dream Air's display

    // A game frame as the poll thread sees it arrive.
    struct InputFrame {
        uint64_t serial = 0;
        double arrival = 0;      // source seconds
        double currentSlot = 0;  // the runtime's next display slot at the arrival
        double slot = 0;         // the slot the timing assigned it (replays: the real tool's)
        int lag = 0;             // drawn for slot + lag periods (replays' true lag; 0 or negative)
        bool synced = true;      // false: an unsynced fallback frame (no frame signal)
    };

    struct Input {
        std::string name;
        std::vector<replay::Pose> poses;              // head samples, in time order
        std::vector<replay::Prediction> predictions;  // per slot, in order of asking
        std::vector<InputFrame> frames;
        double duration = 0;
    };

    // A replay file: its rows as recorded.
    inline bool LoadReplay(const std::string& path, const std::string& name, Input* in, std::string* error) {
        replay::Replay rp;
        if (!rp.Load(path, error)) {
            return false;
        }
        in->name = name;
        in->poses = rp.Poses();
        in->predictions = rp.Predictions();
        in->duration = rp.Duration();

        for (const replay::Frame& fr : rp.Frames()) {
            InputFrame f;
            f.serial = uint64_t(fr.serial);
            f.arrival = fr.arrival;
            f.currentSlot = rp.SlotAt(fr.arrival, kPeriod);
            f.slot = fr.slot;
            f.lag = fr.lag;
            in->frames.push_back(f);
        }

        return true;
    }

    // A synthetic motion (fake_game_motion.h spec) for `seconds`: head samples at 1 kHz, the prediction for
    // each slot exact and asked two periods ahead (the fake's slot announcement), one frame per slot arriving
    // 21.8 ms ahead of it with +-0.5 ms of noise, about 3% dropped, and a few unsynced frames.
    inline bool MakeSynthetic(const std::string& spec, double seconds, Input* in, std::string* error) {
        motion::Motion m;
        if (!m.Parse(spec, error)) {
            return false;
        }

        in->name = spec;
        in->duration = seconds + 1.5;
        for (int i = 1; i <= int(in->duration * 1000); i++) {
            in->poses.push_back({i * 0.001, m.At(i * 0.001)});
        }

        const double firstSlot = 0.5;
        for (int j = 0; firstSlot + j * kPeriod < in->duration + 0.1; j++) {
            const double slot = firstSlot + j * kPeriod;
            replay::Prediction p;
            p.asked = slot - 2 * kPeriod;
            p.slot = slot;
            p.q = m.At(slot);
            in->predictions.push_back(p);
        }

        uint32_t seed = 12345;
        auto next = [&] {  // a fixed LCG: the same input on every build
            seed = seed * 1664525u + 1013904223u;
            return double(seed >> 8) / double(1u << 24);
        };

        uint64_t serial = 0;
        for (int k = 0; firstSlot + k * kPeriod < seconds; k++) {
            const double slot = firstSlot + k * kPeriod, noise = (next() - 0.5) * 0.001, drop = next();
            if (k > 0 && drop < 0.03) {
                continue;
            }
            InputFrame f;
            f.serial = ++serial;
            f.arrival = slot - 0.0218 + noise;
            f.currentSlot = slot;
            f.slot = slot;
            f.synced = k % 97 != 50;
            in->frames.push_back(f);
        }

        return true;
    }

    // The input's poses and predictions as a PoseSource, as PimaxSource gives them: the head interpolated
    // in core's PoseHistory, a slot's prediction within a quarter period (PvrPoses). AdvanceTo
    // feeds it everything up to a moment; it never goes back.
    class ReplayPoses : public PoseSource {
    public:
        explicit ReplayPoses(const Input& in) : in_(in) {}

        void AdvanceTo(double now) {
            for (; pose_ < in_.poses.size() && in_.poses[pose_].t <= now; pose_++) {
                history_.AddPose(in_.poses[pose_].t, in_.poses[pose_].q);
            }

            for (; prediction_ < in_.predictions.size() && in_.predictions[prediction_].asked <= now;
                 prediction_++) {
                history_.AddPrediction(in_.predictions[prediction_].slot, in_.predictions[prediction_].q);
            }
        }

        double Period() const override { return kPeriod; }
        Quat HeadAt(double t) override { return history_.At(t); }
        void HeadsAt(const double* t, int n, Quat* out) override {
            for (int i = 0; i < n; i++) {
                out[i] = history_.At(t[i]);
            }
        }

        bool PredictedHeadAt(double slot, Quat* q) override {
            return history_.PredictionFor(slot, 0.25 * kPeriod, q);
        }

        void PredictedHeadsAt(const double* slots, int n, Quat* out) override {
            for (int i = 0; i < n; i++) {
                out[i] = history_.At(slots[i]);
            }
            history_.PredictionsFor(slots, n, 0.25 * kPeriod, out);
        }

        void PredictedHeadsBetween(const double* t, int n, Quat* out) override {
            for (int i = 0; i < n; i++) {
                out[i] = history_.At(t[i]);
            }
            history_.PredictionsBetween(t, n, 1.5 * kPeriod, out);
        }

    private:
        const Input& in_;
        PoseHistory history_;
        size_t pose_ = 0, prediction_ = 0;
    };

}
