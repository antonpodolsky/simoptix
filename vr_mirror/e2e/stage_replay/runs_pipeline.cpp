// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// RunHost: a pipeline over an input as one of the hosts drives it (the mirror app's window: each frame
// at arrival, or held for the look-ahead delay; the OBS plugin's ticks: each tick's own moment with
// an older frame standing in as the picture, picked by the newest due frame or by the frame
// picker over a store of copies). RunPipeline: any mix of stages through it, with a summary per
// input. Poses and predictions are fed in time order up to each draw (ReplayPoses); where the
// configuration asks for the slot check (or the cockpit correction), frames get their true slot's
// pose (ReplayDrawnPose). Knows nothing about D3D or the fake runtime.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <unordered_map>
#include <vector>

#include "frame_store.h"
#include "pipeline/pipeline_builder.h"
#include "replay_drawn_pose.h"
#include "runs.h"
#include "scores.h"

namespace stage_replay {

    // The Pimax game eye.
    const EyeInfo kEye = {1.195f, 1.173f, 0.984f, 0.984f, {0, 0.0175f, 0, 0.9998f}, true};

    namespace {

        constexpr float kAspect = 16.0f / 9;
        // The OBS plugin's measured copy delay, for the simple ticks, and a copy's time in the store (the
        // frame picker measures it).
        constexpr double kCopyDelay = 0.012;
        constexpr double kCopyTime = 0.002;

        // A frame as its frame source hands it out (arrival de-jittered by the arrival timing, the slot the
        // timing assigned), with the slots before that it was really rendered for (the replays' true lag). A
        // source that reports each frame's slot (ArrivalTimingKind::kReported, as the OpenXR layer) reports
        // the slot it was really drawn for (GameFrame::drawnSlot), so its lag is 0.
        struct HostFrame {
            GameFrame frame;
            int trueLag = 0;
        };

        std::vector<HostFrame> FramesOf(const Input& input, const PipelineConfig& config) {
            const std::unique_ptr<ArrivalTiming> timing = MakeArrivalTiming(config.arrivalTiming);
            const bool reported = config.arrivalTiming == ArrivalTimingKind::kReported;
            std::vector<HostFrame> frames;

            for (const InputFrame& in : input.frames) {
                HostFrame host;
                GameFrame& frame = host.frame;
                frame.buffer = 0;
                frame.serial = in.serial;
                frame.time = frame.smoothTime = in.arrival;

                if (in.synced) {
                    const double drawnFor = in.slot + in.lag * kPeriod;  // what a reporting source says
                    const ArrivalResult result = timing->Add({in.arrival, in.currentSlot, drawnFor, kPeriod});
                    frame.smoothTime = result.smooth;
                    frame.synced = true;

                    // The slots before its shown slot it was really drawn for (a reporting source shows a
                    // late frame in a later slot than it was drawn for, and says which).
                    host.trueLag = reported ? int(std::lround((result.slot - drawnFor) / kPeriod)) : -in.lag;
                    frame.slotTime = reported ? result.slot : in.slot;

                    if (reported) {
                        frame.drawnSlot = drawnFor;
                    }

                    frame.slotLead = in.currentSlot - in.arrival;
                }

                frames.push_back(host);
            }

            return frames;
        }

        class HostDriver {
        public:
            HostDriver(const HostRun& run, const std::function<void(const HostOutput&)>& output);

            void DrawWindow();
            void DrawNewestDue();
            void DrawPicked();

        private:
            void Draw(int index, const FrameTimes& view, const FrameTimes* image, int trueLag, double now);

            const HostRun& run_;
            const std::function<void(const HostOutput&)>& output_;
            const std::vector<HostFrame> frames_;
            std::unique_ptr<ReplayPoses> poses_;
            MirrorSettings settings_;
            Pipeline pipeline_;
            double lookahead_ = 0;
            uint32_t ticks_ = 0;
            HostOutput out_;
        };

        HostDriver::HostDriver(const HostRun& run, const std::function<void(const HostOutput&)>& output)
            : run_(run),
              output_(output),
              frames_(FramesOf(*run.input, run.config)),
              poses_(std::make_unique<ReplayPoses>(*run.input)),
              settings_(run.settings) {
            BuildPipeline(run.config, &pipeline_);

            // The slot check judges real pictures, which these runs don't have: each frame its true slot.
            const DrawnPoseKind kind = run.config.drawnPose;

            if (kind == DrawnPoseKind::kSlotCheck || kind == DrawnPoseKind::kCockpit) {
                auto truth = std::make_shared<ReplayDrawnPose>();

                for (const HostFrame& host : frames_) {
                    truth->Set(host.frame.serial, host.trueLag);
                }

                pipeline_.Swap(std::move(truth), kind);
            }

            lookahead_ = SmoothingDelayS(pipeline_.Config().smoothing);
            out_.pipeline = &pipeline_;
        }

        // What the host does at output `index` before it draws: stabilization switched off and on (the
        // configuration stays: off runs the same stages, without stabilizing).
        void HostDriver::Draw(
            int index,
            const FrameTimes& view,
            const FrameTimes* image,
            int trueLag,
            double now
        ) {
            if (index == run_.stabilizeOff) {
                settings_.smoothing = 0;
                pipeline_.Reset();
            }

            if (index == run_.stabilizeOn) {
                settings_.smoothing = run_.settings.smoothing;
                pipeline_.Reset();
            }

            ReconfigurePipeline(run_.config, &pipeline_);  // once per output frame, as the hosts do

            out_.index = index;
            out_.view = view;
            out_.image = image;
            out_.trueLag = trueLag;
            out_.warp = pipeline_.Update(settings_, *poses_, kEye, {view, image, now, kAspect});
            output_(out_);
        }

        // The window: Instant draws each frame right after it arrives; the look-ahead holds it and
        // draws it just before it is due (the mirror app's present delay, 4 ms, included).
        void HostDriver::DrawWindow() {
            const bool hold = run_.config.smoothing != SmoothingKind::kInstant;

            for (int k = 0; k < int(frames_.size()); k++) {
                const GameFrame& frame = frames_[k].frame;
                const double now = hold ? frame.smoothTime + lookahead_ + 0.004 - 0.002 : frame.time + 0.0003;
                poses_->AdvanceTo(now);

                Draw(k, TimesOf(frame), nullptr, frames_[k].trueLag, now);
            }
        }

        // Each tick shows the newest frame due by then (its de-jittered arrival plus the output
        // delay) and is warped to the head pose of its own moment, the tick minus that delay.
        void HostDriver::DrawNewestDue() {
            const double outDelay = kCopyDelay + lookahead_;
            auto due = [&](size_t k) { return frames_[k].frame.smoothTime + outDelay; };
            const double first = due(0) + 0.5;
            const double last = due(frames_.size() - 1);

            size_t shown = 0;
            bool any = false;

            for (int i = 0; first + i / run_.tickHz <= last; i++) {
                const double tick = first + i / run_.tickHz;

                while (shown + 1 < frames_.size() && due(shown + 1) <= tick) {
                    shown++;
                    any = true;
                }

                if (!any && due(shown) <= tick) {
                    any = true;
                }

                if (!any) {
                    continue;
                }

                poses_->AdvanceTo(tick);

                const FrameTimes view = MomentTimes((1ull << 62) | ++ticks_, tick - outDelay);
                const FrameTimes image = TimesOf(frames_[shown].frame);
                Draw(i, view, &image, frames_[shown].trueLag, tick);
            }
        }

        // The frame picker over a store of copies, as the plugin runs it: synced frames are copied as
        // they arrive, each copy done kCopyTime later.
        void HostDriver::DrawPicked() {
            FramePicker& picker = *pipeline_.Picker();
            auto store = std::make_unique<FrameStore>();
            picker.Restart(lookahead_, FrameStore::kMaxSlots);
            store->Resize(picker.SlotsInUse());

            std::unordered_map<uint64_t, size_t> bySerial;

            for (size_t k = 0; k < frames_.size(); k++) {
                bySerial[frames_[k].frame.serial] = k;
            }

            const double first = frames_.front().frame.time + 0.5;
            const double last = frames_.back().frame.time + picker.OutDelay();
            size_t next = 0;

            for (int i = 0; first + i / run_.tickHz <= last; i++) {
                const double tick = first + i / run_.tickHz;

                for (; next < frames_.size() && frames_[next].frame.time <= tick; next++) {
                    const GameFrame& frame = frames_[next].frame;

                    if (frame.synced) {
                        store->Add(
                            frame.serial,
                            frame.time,
                            frame.smoothTime,
                            frame.slotTime,
                            frame.time + kCopyTime
                        );
                    }
                }

                store->Finish(tick);

                const Pick pick = picker.Tick(tick, kPeriod, store->slots, FrameStore::kMaxSlots);

                if (picker.SlotsInUse() != store->count) {
                    store->Resize(picker.SlotsInUse());  // the delay fixed
                }

                if (pick.kind == PickKind::kNone) {
                    continue;
                }

                const HostFrame& shown = frames_[bySerial[store->slots[pick.index].serial]];
                poses_->AdvanceTo(tick);

                const FrameTimes view = MomentTimes((1ull << 62) | ++ticks_, pick.moment);
                const FrameTimes image = TimesOf(shown.frame);
                Draw(i, view, &image, shown.trueLag, tick);
            }
        }

    }

    void RunHost(const HostRun& run, const std::function<void(const HostOutput&)>& output) {
        HostDriver host(run, output);

        if (!run.ticks) {
            host.DrawWindow();
        } else if (!run.pickByStage) {
            host.DrawNewestDue();
        } else {
            host.DrawPicked();
        }
    }

    int RunPipeline(
        const PipelineConfig& config,
        const MirrorSettings& settings,
        const std::vector<const Input*>& inputs,
        double tickHz
    ) {
        printf(
            "pipeline: arrival_timing %s, drawn_pose %s, frame_picker %s, smoothing %s, warp %s; "
            "zoom %.2f; %s\n",
            NameOf(config.arrivalTiming),
            NameOf(config.drawnPose),
            NameOf(config.framePicker),
            NameOf(config.smoothing),
            NameOf(config.warp),
            settings.zoom,
            tickHz > 0 ? "OBS ticks" : "window"
        );

        if (tickHz > 0) {
            printf("  (ticks at %.2f Hz, frames picked by the frame picker)\n", tickHz);
        }

        printf("  (lower is better in every measure; lag: the view's look direction behind the picture's)\n");

        for (const Input* input : inputs) {
            HostRun run;
            run.input = input;
            run.settings = settings;
            run.config = config;
            run.ticks = tickHz > 0;
            run.tickHz = tickHz;
            run.pickByStage = true;

            Scores scores(*input);
            RunHost(run, [&](const HostOutput& out) { scores.Add(out); });
            scores.Print(input->name.c_str());
        }

        return 0;
    }

}
