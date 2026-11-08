// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// stage_replay --stage frame_picker: runs the frame picker (stage 3) the way the OBS plugin does,
// over OBS frame traces (their ticks and frame arrivals) and synthetic ticks at 50, 59.94, 60 and
// 144 Hz, with copies finishing after a seeded random time (now and then late) and a store that
// shrinks and grows as the picker asks; prints the picks by kind and the delay it fixed. Checked
// identical to the plugin's picking before the stage (vr_mirror\e2e\README.md, "Stage replay").
// Knows nothing about textures or OBS.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "frame_store.h"
#include "pipeline/pipeline_builder.h"
#include "runs.h"

namespace stage_replay {

    namespace {

        constexpr int kMaxSlots = FrameStore::kMaxSlots;
        constexpr double kDisplayPeriod = 1.0 / 90;

        struct TraceFrame {
            uint64_t serial = 0;
            double arrival = 0, smooth = 0, slot = 0;
        };

        struct TraceInput {
            std::string name;
            std::vector<TraceFrame> frames;
            std::vector<double> ticks;
            std::vector<std::pair<size_t, int>> smoothingAt;  // (tick index, settings smoothing) from then on
        };

        bool LoadTrace(const std::string& ticksPath, const std::string& pimaxPath, TraceInput* in) {
            FILE* f = nullptr;
            char line[512];
            if (fopen_s(&f, pimaxPath.c_str(), "r") != 0 || !f) {
                return false;
            }
            while (fgets(line, sizeof(line), f)) {
                TraceFrame fr;
                unsigned long long serial = 0;
                if (line[0] == 'F'
                    && sscanf_s(line, "F,%llu,%lf,%lf,%lf", &serial, &fr.arrival, &fr.smooth, &fr.slot)
                           == 4) {
                    fr.serial = serial;
                    in->frames.push_back(fr);
                }
            }

            fclose(f);
            if (fopen_s(&f, ticksPath.c_str(), "r") != 0 || !f) {
                return false;
            }
            while (fgets(line, sizeof(line), f)) {
                unsigned stamp = 0;
                double tick = 0;
                if (sscanf_s(line, "%u,%lf", &stamp, &tick) == 2) {
                    in->ticks.push_back(tick);
                }
            }

            fclose(f);
            return !in->frames.empty() && !in->ticks.empty();
        }

        // Frames at 90 Hz for `seconds` (arrival noise, ~3% dropped, a stall of 20 frames) and ticks at
        // `hz` with a little jitter; the smoothing changes twice.
        TraceInput Synthetic(double hz, double seconds) {
            TraceInput in;
            char name[64];
            snprintf(name, sizeof(name), "synthetic %.2f Hz", hz);
            in.name = name;
            uint32_t seed = 777u + uint32_t(hz * 100);
            auto next = [&] {
                seed = seed * 1664525u + 1013904223u;
                return double(seed >> 8) / double(1u << 24);
            };

            uint64_t serial = 0;
            for (int k = 0; k * kDisplayPeriod < seconds; k++) {
                const double slot = 100 + k * kDisplayPeriod;
                if (next() < 0.03 || (k > 500 && k < 520)) {
                    continue;
                }
                TraceFrame f;
                f.serial = ++serial;
                f.arrival = slot - 0.0218 + (next() - 0.5) * 0.001;
                f.smooth = slot - 0.0218;
                f.slot = slot;
                in.frames.push_back(f);
            }

            for (int i = 0; 100 + i / hz < 100 + seconds; i++) {
                in.ticks.push_back(100 + i / hz + (next() - 0.5) * 0.0005);
            }

            in.smoothingAt = {{0, 2}, {in.ticks.size() / 3, 0}, {2 * in.ticks.size() / 3, 1}};
            return in;
        }

        double Median(std::vector<double>* v) {
            if (v->empty()) {
                return 0;
            }
            std::nth_element(v->begin(), v->begin() + v->size() / 2, v->end());
            return (*v)[v->size() / 2];
        }

        void Run(const TraceInput& in, int smoothing, FramePickerKind kind) {
            auto store = std::make_unique<FrameStore>();
            PipelineConfig config;
            config.framePicker = kind;
            config.smoothing = SmoothingKind(smoothing);

            std::unique_ptr<FramePicker> picker = MakeFramePicker(config.framePicker);
            picker->Restart(SmoothingDelayS(config.smoothing), kMaxSlots);
            store->Resize(picker->SlotsInUse());
            double outDelay = picker->OutDelay();  // as last seen: a change is the delay fixed

            uint32_t seed = 4242;
            auto next = [&] {
                seed = seed * 1664525u + 1013904223u;
                return double(seed >> 8) / double(1u << 24);
            };

            size_t frame = 0, change = 0;
            int counts[5] = {}, fixedTick = -1;
            double fixedDelay = 0, jump = 0, lastTick = 0, lastMoment = 0;
            std::vector<double> behind;  // per pick: the shown picture's slot minus the tick's moment (s)

            for (size_t t = 0; t < in.ticks.size(); t++) {
                const double tick = in.ticks[t];
                if (change < in.smoothingAt.size() && in.smoothingAt[change].first == t) {
                    const SmoothingKind k = SmoothingKind(in.smoothingAt[change++].second);
                    if (k != config.smoothing) {
                        config.smoothing = k;
                        picker->Restart(SmoothingDelayS(k), kMaxSlots);
                        store->Resize(picker->SlotsInUse());
                        outDelay = picker->OutDelay();
                        lastTick = 0;  // the moments move with the smoothing's delay: not a jump
                    }
                }

                // Frames arrived by this tick go into the store; copies take 1-3 ms, 5% of them 5-30 ms.
                for (; frame < in.frames.size() && in.frames[frame].arrival <= tick; frame++) {
                    const double copy = next() < 0.05 ? 0.005 + 0.025 * next() : 0.001 + 0.002 * next();
                    const TraceFrame& f = in.frames[frame];
                    store->Add(f.serial, f.arrival, f.smooth, f.slot, f.arrival + copy);
                }

                store->Finish(tick);
                const Pick p = picker->Tick(tick, kDisplayPeriod, store->slots, kMaxSlots);
                if (picker->OutDelay() != outDelay) {  // the delay fixed
                    outDelay = picker->OutDelay();
                    fixedDelay = outDelay - SmoothingDelayS(config.smoothing);
                    fixedTick = int(t);
                    store->Resize(picker->SlotsInUse());
                }

                counts[int(p.kind)]++;
                if (p.kind == PickKind::kNone) {
                    continue;
                }

                // The tick's moment: how far its view trails the shown picture's slot, and how far it moved
                // other than with the ticks (a jump of the whole view).
                const StoredFrame& shown = store->slots[p.index];
                if (shown.slotTime > 0) {
                    behind.push_back(shown.slotTime - p.moment);
                }
                if (lastTick > 0) {
                    jump = std::max(jump, std::abs((p.moment - lastMoment) - (tick - lastTick)));
                }
                lastTick = tick;
                lastMoment = p.moment;
            }

            printf(
                "%-40s %s, smoothing %d: %zu ticks, %d next, %d repeat, %d jump, %d held, %d none; delay "
                "fixed at "
                "tick "
                "%d (%.1f ms), %d entries; view behind its picture %.1f ms (median), moment jumps up to %.1f "
                "ms\n",
                in.name.c_str(),
                NameOf(kind),
                smoothing,
                in.ticks.size(),
                counts[0],
                counts[1],
                counts[2],
                counts[3],
                counts[4],
                fixedTick,
                fixedDelay * 1000,
                picker->SlotsInUse(),
                Median(&behind) * 1000,
                jump * 1000
            );
        }

    }

    int RunFramePicker(const std::vector<std::pair<std::string, std::string>>& traces, FramePickerKind kind) {
        for (const auto& t : traces) {
            TraceInput in;
            in.name = t.first.substr(t.first.find_last_of("\\/") + 1);
            if (!LoadTrace(t.first, t.second, &in)) {
                printf("cannot read %s / %s\n", t.first.c_str(), t.second.c_str());
                return -1;
            }

            for (int smoothing = 0; smoothing <= 2; smoothing++) {
                Run(in, smoothing, kind);
            }
        }

        for (double hz : {50.0, 59.94, 60.0, 144.0}) {
            Run(Synthetic(hz, 30.0), 2, kind);
        }
        return 0;
    }

}
