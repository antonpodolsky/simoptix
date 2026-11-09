// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// stage_replay --stage drawn_pose (was lag_sim, then --stage frame_matching): the drawn pose
// (stage 2, vr_mirror\core\pipeline\2_drawn_pose) replayed on an e2e replay, without the tool, the fake or
// the GPU: each frame's real thumbnail (<replay>.thumbs.png, as the fake shows it and the tool reads it), its
// slot and arrival, and the runtime's recorded pose predictions go straight into the drawn pose the
// configuration names (the slot check unless --drawn_pose says otherwise), and its poses are scored against
// the ones the frames were really drawn with (the replay's true lags). The replays hold no patches, so the
// cockpit correction has nothing to measure here. Deterministic and quick, for finding out why a replay fails
// and trying changes; e2e decides.
//
// For each frame it scores the pose as each host asks for it:
//   prev  the mirror app with Instant: drawn at once, before its own thumbnail is read
//   own   the OBS plugin with Instant: drawn a few ms later, after its own thumbnail
//   h45   45 frames later (Look-ahead: shown half a second after arrival)
//   h92   92 frames later (a second of hindsight)
// A pose more than 0.01 deg off the true one is wrong. --show also prints, per frame, which slot's
// prediction each pose matches (by comparing poses: the slots before its arrival slot, ? for none)
// and the slot check's evidence for the pair of frames.
//
// Usage: stage_replay.exe --stage drawn_pose <replay .csv> [--show FIRST-LAST] [--dump FILE]
//        [--delays A,B,...] [--drawn_pose K]   (--show: serials to print frame by frame)

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "fake_pvr_replay.h"
#include "fake_pvr_thumbs.h"
#include "pipeline/2_drawn_pose/slot_check_drawn_pose_lag.h"
#include "pipeline/pipeline_builder.h"
#include "replay_poses.h"
#include "runs.h"

namespace {

    using stage_replay::kPeriod;
    // LMU's left eye on the Dream Air, as the fake reports it (vr_mirror\e2e\fake_pvr).
    const EyeInfo kEye = {
        1.195f,
        1.173f,
        0.984f,
        0.984f,
        {0, 0.0175f, 0, 0.9998f},
        true,
    };  // the Pimax game eye
    constexpr double kWrongDeg = 0.01;

    // The replay's predictions by slot, as PimaxSource records them (one per slot, asked when it became
    // the next), looked up within a quarter period as PoseHistory does: the feeding's lookups.
    class ReplayPredictions : public PoseLookup {
    public:
        explicit ReplayPredictions(const replay::Replay& r) : replay_(r) {}
        bool PredictedHeadAt(double slot, Quat* q) const override {
            return replay_.PredictionFor(slot, kPeriod, q);
        }

    private:
        const replay::Replay& replay_;
    };

    double AngleDeg(const Quat& a, const Quat& b) {
        const Vec3 v = RotationVector(Mul(Conj(a), b));
        return std::sqrt(double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z) * 180 / 3.14159265358979;
    }

    const int kD45 = 45, kD92 = 92;

    struct Options {
        std::string path;
        long long showFirst = 0, showLast = -1;
        std::string dumpPath;     // --dump FILE: every frame's timing and pair costs, as CSV
        std::vector<int> delays;  // --delays A,B,...: also score the poses that many frames later
    };

    // argv[1]: the replay, then its options (as lag_sim took them).
    Options ParseOptions(int argc, char** argv) {
        Options o;
        o.path = argv[1];
        for (int i = 2; i + 1 < argc; i++) {
            if (std::string(argv[i]) == "--show") {
                sscanf_s(argv[++i], "%lld-%lld", &o.showFirst, &o.showLast);
            } else if (std::string(argv[i]) == "--dump") {
                o.dumpPath = argv[++i];
            } else if (std::string(argv[i]) == "--delays") {
                for (const char* p = argv[++i]; *p;) {
                    o.delays.push_back(std::atoi(p));
                    while (*p && *p != ',') {
                        p++;
                    }
                    if (*p == ',') {
                        p++;
                    }
                }
            }
        }

        return o;
    }

    // One frame's truth, the poses the drawn pose gave for it, and the slot check's evidence.
    struct Row {
        int truth = 0;
        Quat truePose, prev, own, h45, h92;
        bool hasTruth = false, hasH45 = false, hasH92 = false;
        bool gate = false;

        // the pair's costs (as the slot check computes them)
        float step[LagFilter::kLags][LagFilter::kLags] = {};
        bool hasStep = false;

        // the same against the frame before the previous
        float step2[LagFilter::kLags][LagFilter::kLags] = {};
        bool hasStep2 = false;
    };

    ThumbView ThumbViewOfEye() {
        ThumbView view;
        const float* area = kThumbArea;
        view.left = kEye.leftTan - area[0] * (kEye.leftTan + kEye.rightTan);
        view.right = -kEye.leftTan + area[2] * (kEye.leftTan + kEye.rightTan);
        view.up = kEye.upTan - area[1] * (kEye.upTan + kEye.downTan);
        view.down = -kEye.upTan + area[3] * (kEye.upTan + kEye.downTan);
        view.cant = kEye.hmdToEye;
        return view;
    }

    // The re-projection error, over `sens`, of each pairing of an earlier thumbnail at the poses `pa` (one
    // per lag) with a later one at the poses `pb`.
    void PairCosts(
        const std::vector<float>& before,
        const Quat* pa,
        const std::vector<float>& after,
        const Quat* pb,
        const ThumbView& view,
        float sens,
        float (*out)[LagFilter::kLags]
    ) {
        for (int a = 0; a < LagFilter::kLags; a++) {
            for (int b = 0; b < LagFilter::kLags; b++) {
                out[a][b] = ReprojectionError(before, pa[a], after, pb[b], view) / sens;
            }
        }
    }

    class DrawnPoseReplay {
    public:
        DrawnPoseReplay(const Options& options, DrawnPoseKind kind)
            : options_(options),
              kind_(kind),
              lookup_(rp_) {}

        bool Load(std::string* error);
        void Feed();
        void Report() const;
        void Dump() const;

    private:
        void FeedFrame(size_t k);
        void PairEvidence(size_t k, const GameFrame& f, Row* r);
        bool Wrong(const Row& r, const Quat& q, bool has) const;
        int UsedSlot(size_t k, const Quat& q) const;
        void Show(size_t k, const Row& r, const bool w[4]) const;

        const Options options_;
        const DrawnPoseKind kind_;
        replay::Replay rp_;
        ReplayPredictions lookup_;
        stage_replay::Input input_;
        std::vector<uint8_t> rgba_;
        int tiles_ = 0;

        // The answers' poses, fed in time order up to each frame's draw, as a host has them.
        std::unique_ptr<stage_replay::ReplayPoses> poses_;
        std::shared_ptr<DrawnPose> drawnPose_;
        PictureNeeds needs_;
        std::vector<float> areas_;
        const std::vector<float> noPatches_;
        static constexpr size_t kTexels = size_t(thumbs::kWidth) * thumbs::kHeight;
        std::vector<float> thumb_ = std::vector<float>(kTexels * 3);

        std::vector<Row> rows_;
        std::vector<FrameTimes> times_;
        std::vector<float> prevThumb_, prev2Thumb_;
        GameFrame prevFrame_, prev2Frame_;
        TimingGate gate_;  // the slot check's gate, for --show and --dump
        std::vector<std::vector<Quat>> atDelay_;
        std::vector<std::vector<bool>> hasDelay_;
    };

    bool DrawnPoseReplay::Load(std::string* error) {
        if (!rp_.Load(options_.path, error)) {
            return false;
        }
        if (!stage_replay::LoadReplay(options_.path, "replay", &input_, error)) {
            return false;
        }

        const std::string thumbsPath = options_.path.substr(0, options_.path.rfind('.')) + ".thumbs.png";
        tiles_ = thumbs::Load(thumbsPath, &rgba_, error);
        if (!tiles_) {
            return false;
        }

        poses_ = std::make_unique<stage_replay::ReplayPoses>(input_);
        drawnPose_ = MakeDrawnPose(kind_);
        needs_ = drawnPose_->Needs();
        areas_.resize(size_t(std::max(1, needs_.patches.count)) * 4);

        const size_t frames = rp_.Frames().size();
        rows_.resize(frames);
        times_.resize(frames);
        atDelay_.assign(options_.delays.size(), std::vector<Quat>(frames));
        hasDelay_.assign(options_.delays.size(), std::vector<bool>(frames, false));
        return true;
    }

    void DrawnPoseReplay::Feed() {
        const auto& frames = rp_.Frames();
        for (size_t k = 0; k < frames.size() && int(k) < tiles_; k++) {
            FeedFrame(k);
        }
    }

    void DrawnPoseReplay::FeedFrame(size_t k) {
        const replay::Frame& fr = rp_.Frames()[k];
        DrawnPose& stage = *drawnPose_;

        // The runtime's next display slot at the frame's arrival (the slot lead's base).
        const double currentSlot = rp_.SlotAt(fr.arrival, kPeriod);

        GameFrame f;
        f.buffer = 0;
        f.serial = uint64_t(fr.serial);
        f.time = f.smoothTime = fr.arrival;
        f.synced = true;
        f.slotTime = fr.slot;
        f.slotLead = currentSlot > 0 ? currentSlot - fr.arrival : 0;

        const uint8_t* px = &rgba_[k * kTexels * 4];
        for (size_t t = 0; t < kTexels; t++) {
            for (int c = 0; c < 3; c++) {
                thumb_[t * 3 + c] = px[t * 4 + c] / 255.0f;
            }
        }

        times_[k] = TimesOf(f);
        poses_->AdvanceTo(fr.arrival + 0.004);  // drawn a few ms after arrival (the OBS plugin at Instant)

        Row& r = rows_[k];
        r.truth = -fr.lag;
        r.hasTruth = lookup_.PredictedHeadAt(fr.slot - r.truth * kPeriod, &r.truePose);
        r.gate = gate_.Update(f.time, f.slotLead, kPeriod);

        // The pair's evidence, for --show: the same costs the slot check computes.
        if (k > 0 && prevFrame_.serial + 1 == f.serial) {
            PairEvidence(k, f, &r);
        }

        prev2Thumb_ = prevThumb_;
        prev2Frame_ = prevFrame_;
        prevThumb_ = thumb_;
        prevFrame_ = f;
        r.prev = stage.PoseOf(times_[k], *poses_);  // before its own thumbnail is read
        if (needs_.thumbnail) {  // as the feed: a drawn pose that looks at no pictures isn't fed
            stage.Arrived(f, lookup_, kEye, kPeriod, reinterpret_cast<float (*)[4]>(areas_.data()));
            stage.Look(f, thumb_, noPatches_, lookup_, kEye, kPeriod);
        }

        r.own = stage.PoseOf(times_[k], *poses_);
        if (k >= kD45) {
            rows_[k - kD45].h45 = stage.PoseOf(times_[k - kD45], *poses_), rows_[k - kD45].hasH45 = true;
        }

        if (k >= kD92) {
            rows_[k - kD92].h92 = stage.PoseOf(times_[k - kD92], *poses_), rows_[k - kD92].hasH92 = true;
        }

        for (size_t di = 0; di < options_.delays.size(); di++) {
            const int delay = options_.delays[di];
            if (int(k) < delay) {
                continue;
            }
            atDelay_[di][k - delay] = stage.PoseOf(times_[k - delay], *poses_);
            hasDelay_[di][k - delay] = true;
        }
    }

    void DrawnPoseReplay::PairEvidence(size_t k, const GameFrame& f, Row* r) {
        const ThumbView view = ThumbViewOfEye();

        Quat pa[LagFilter::kLags], pb[LagFilter::kLags];
        bool ok = true;
        for (int i = 0; i < LagFilter::kLags && ok; i++) {
            ok = lookup_.PredictedHeadAt(prevFrame_.slotTime - i * kPeriod, &pa[i])
                 && lookup_.PredictedHeadAt(f.slotTime - i * kPeriod, &pb[i]);
        }

        const float sens = ok ? RotationSensitivity(thumb_, pb[0], view) : 0;
        if (sens <= 0) {
            return;
        }

        r->hasStep = true;
        PairCosts(prevThumb_, pa, thumb_, pb, view, sens, r->step);

        Quat p2[LagFilter::kLags];
        bool ok2 = k > 1 && prev2Frame_.serial + 2 == f.serial;
        for (int i = 0; i < LagFilter::kLags && ok2; i++) {
            ok2 = lookup_.PredictedHeadAt(prev2Frame_.slotTime - i * kPeriod, &p2[i]);
        }

        if (!ok2) {
            return;
        }
        r->hasStep2 = true;
        PairCosts(prev2Thumb_, p2, thumb_, pb, view, sens, r->step2);
    }

    bool DrawnPoseReplay::Wrong(const Row& r, const Quat& q, bool has) const {
        return !has || AngleDeg(q, r.truePose) > kWrongDeg;
    }

    // Which slot's prediction a pose matches, by comparing poses: the slots before the arrival slot (0
    // to 3), or -1 for none.
    int DrawnPoseReplay::UsedSlot(size_t k, const Quat& q) const {
        for (int lag = 0; lag < LagFilter::kLags; lag++) {
            Quat p;
            const double slot = rp_.Frames()[k].slot - lag * kPeriod;
            if (lookup_.PredictedHeadAt(slot, &p) && AngleDeg(q, p) <= kWrongDeg) {
                return lag;
            }
        }

        return -1;
    }

    // One frame's line for --show: which slot each pose matches (* when it is wrong) and the pair's costs.
    void DrawnPoseReplay::Show(size_t k, const Row& r, const bool w[4]) const {
        auto lag = [&](const Quat& q) {
            static char buf[4][8];
            static int slot = 0;
            char* b = buf[slot++ % 4];
            const int u = UsedSlot(k, q);
            if (u < 0) {
                snprintf(b, 8, "?");
            } else {
                snprintf(b, 8, "%d", u);
            }

            return b;
        };

        printf(
            "%lld  true %d  gate %d   prev %s%s own %s%s h45 %s%s h92 %s%s",
            rp_.Frames()[k].serial,
            r.truth,
            r.gate ? 1 : 0,
            lag(r.prev),
            w[0] ? "*" : " ",
            lag(r.own),
            w[1] ? "*" : " ",
            lag(r.h45),
            w[2] ? "*" : " ",
            lag(r.h92),
            w[3] ? "*" : " "
        );

        if (r.hasStep) {
            // The pair at one lag throughout, and at the true change from the previous frame's lag.
            const int pt = rows_[k - 1].truth;
            printf(
                "   same 00 %.3f 11 %.3f 22 %.3f 33 %.3f  true pair %d%d %.3f",
                r.step[0][0],
                r.step[1][1],
                r.step[2][2],
                r.step[3][3],
                pt,
                r.truth,
                r.step[pt][r.truth]
            );
        }

        printf("\n");
    }

    void DrawnPoseReplay::Report() const {
        const auto& frames = rp_.Frames();

        // Scored from a second in (the tool starts and settles first) to the last frame with a hindsight.
        const size_t first = 90, last = std::min(frames.size(), size_t(tiles_)) - kD92;
        int bad[4] = {}, n = 0, late = 0;
        for (size_t k = first; k < last; k++) {
            const Row& r = rows_[k];
            if (!r.hasTruth) {
                continue;
            }
            n++;
            late += r.truth != 0;
            const bool w[4] = {
                Wrong(r, r.prev, true),
                Wrong(r, r.own, true),
                Wrong(r, r.h45, r.hasH45),
                Wrong(r, r.h92, r.hasH92)
            };

            for (int i = 0; i < 4; i++) {
                bad[i] += w[i];
            }
            if (frames[k].serial >= options_.showFirst && frames[k].serial <= options_.showLast) {
                Show(k, r, w);
            }
        }

        if (n == 0) {
            n = 1;
        }
        printf(
            "%d frames (%d late), drawn_pose %s: wrong pose (over %.2f deg off; lower is better)  prev "
            "%.1f%%  "
            "own %.1f%%  h45 %.1f%%  h92 %.1f%%\n",
            n,
            late,
            NameOf(kind_),
            kWrongDeg,
            100.0 * bad[0] / n,
            100.0 * bad[1] / n,
            100.0 * bad[2] / n,
            100.0 * bad[3] / n
        );

        if (options_.delays.empty()) {
            return;
        }
        printf("wrong pose after:");
        for (size_t di = 0; di < options_.delays.size(); di++) {
            int w = 0;
            for (size_t k = first; k < last; k++) {
                if (rows_[k].hasTruth) {
                    w += Wrong(rows_[k], atDelay_[di][k], hasDelay_[di][k]);
                }
            }

            printf("  %d frames %.1f%%", options_.delays[di], 100.0 * w / n);
        }

        printf("\n");
    }

    void DrawnPoseReplay::Dump() const {
        if (options_.dumpPath.empty()) {
            return;
        }

        // Every frame's timing and evidence, for trying other decision rules offline. h92: the slot its
        // pose 92 frames later matches (slots before its arrival slot, by comparing poses; -1: none).
        const auto& frames = rp_.Frames();
        FILE* d = nullptr;
        if (fopen_s(&d, options_.dumpPath.c_str(), "w") != 0 || !d) {
            return;
        }

        fprintf(d, "serial,truth,gate,arrival,slot,h92,has1,has2,step1[16],step2[16]\n");
        for (size_t k = 0; k < rows_.size() && int(k) < tiles_; k++) {
            const Row& r = rows_[k];
            const int u = r.hasH92 ? UsedSlot(k, r.h92) : -1;
            fprintf(
                d,
                "%lld,%d,%d,%.6f,%.6f,%d,%d,%d",
                frames[k].serial,
                r.truth,
                r.gate ? 1 : 0,
                frames[k].arrival,
                frames[k].slot,
                u,
                r.hasStep ? 1 : 0,
                r.hasStep2 ? 1 : 0
            );

            for (int a = 0; a < LagFilter::kLags; a++) {
                for (int b = 0; b < LagFilter::kLags; b++) {
                    fprintf(d, ",%.5f", r.step[a][b]);
                }
            }

            for (int a = 0; a < LagFilter::kLags; a++) {
                for (int b = 0; b < LagFilter::kLags; b++) {
                    fprintf(d, ",%.5f", r.step2[a][b]);
                }
            }

            fprintf(d, "\n");
        }

        fclose(d);
    }

}

static int DrawnPoseRun(int argc, char** argv, DrawnPoseKind kind) {
    if (argc < 2) {
        printf("usage: stage_replay.exe --stage drawn_pose <replay .csv> [--show FIRST-LAST]\n");
        return 2;
    }

    DrawnPoseReplay run(ParseOptions(argc, argv), kind);
    std::string error;
    if (!run.Load(&error)) {
        printf("%s\n", error.c_str());
        return 1;
    }

    run.Feed();
    run.Report();
    run.Dump();
    return 0;
}

namespace stage_replay {

    int RunDrawnPose(int argc, char** argv, const PipelineConfig& config) {
        return DrawnPoseRun(argc, argv, config.drawnPose);
    }

}
