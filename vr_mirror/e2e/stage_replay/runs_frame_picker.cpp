// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// stage_replay --stage frame_picker: runs the frame picker (stage 3) the way the OBS plugin does,
// over OBS frame traces (their ticks and frame arrivals) and synthetic frames (a 90 Hz headset with
// OBS ticking at 50, 59.94, 60, 90 and 144 fps, and a 144 Hz headset with OBS at 60 fps; each set
// with jittered and with exact ticks), with copies finishing after a seeded random time (now and then
// late) and a store that shrinks and grows as the picker asks; prints the picks by kind, the delay it
// fixed, what the store held (the frames shown, the frames still stored when due, the most entries
// holding a frame), and the picker's forecast's answers against what was shown. Checked identical to
// the plugin's picking before the stage (vr_mirror\e2e\README.md, "Stage replay"). Knows nothing
// about textures or OBS.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

#include "frame_store.h"
#include "pipeline/pipeline_builder.h"
#include "runs.h"

namespace stage_replay {

    namespace {

        constexpr int kMaxSlots = FrameStore::kMaxSlots;

        struct TraceFrame {
            uint64_t serial = 0;
            double arrival = 0, smooth = 0, slot = 0;
        };

        struct TraceInput {
            std::string name;
            std::vector<TraceFrame> frames;
            std::vector<double> ticks;
            std::vector<std::pair<size_t, int>> smoothingAt;       // (tick index, SmoothingKind) from then on
            double period = 1.0 / 90;                              // the headset's display period (s)
            double tickPeriod = 0;                                 // OBS's frame period (s)
            std::vector<std::pair<uint64_t, double>> shownSmooth;  // a trace's: (serial, de-jittered arrival)
        };

        // What the store held over a run, for the memory a picker needs. A frame no tick shows was
        // copied for nothing; a frame gone from the store before it was due was let go of early.
        struct FrameUse {
            size_t stored = 0, shown = 0, keptUntilDue = 0, goneBeforeDue = 0;
            size_t skipped = 0;   // of the frames due: not copied, the forecast said no tick shows them
            size_t copies = 0;    // copies made
            int mostHolding = 0;  // entries holding a frame at once
            uint64_t lastShown = 0;
            size_t nextDue = 0;  // the first stored frame not yet due
        };

        double Median(std::vector<double>* v) {
            if (v->empty()) {
                return 0;
            }
            std::nth_element(v->begin(), v->begin() + v->size() / 2, v->end());
            return (*v)[v->size() / 2];
        }

        // Each frame's place in `in.frames`, by serial.
        std::unordered_map<uint64_t, size_t> IndexBySerial(const TraceInput& in) {
            std::unordered_map<uint64_t, size_t> indexOf;

            for (size_t i = 0; i < in.frames.size(); i++) {
                indexOf[in.frames[i].serial] = i;
            }

            return indexOf;
        }

        // A repeat the game didn't cause: the frame after the shown one (`shown`, its place in
        // `in.frames`) was due by the tick, but not shown (its copy late, or the picker behind). After a
        // dropped game frame no newer frame is due, and the repeat is the game's.
        bool NewerFrameDue(const TraceInput& in, size_t shown, double tick, double outDelay) {
            return shown + 1 < in.frames.size() && in.frames[shown + 1].smooth + outDelay <= tick;
        }

        // The picker's forecast's answers, asked for each frame once the next one has arrived with the
        // picker's newest record, held against what the ticks then showed. A frame it said no tick
        // shows, shown after all, is an error: a host that skips its copy would have nothing to show.
        class WillShowCheck {
        public:
            WillShowCheck(const TraceInput& in, const std::unordered_map<uint64_t, size_t>& indexOf)
                : in_(in),
                  indexOf_(indexOf),
                  answer_(in.frames.size(), kNotAsked),
                  shown_(in.frames.size(), false) {}

            // Frame `next` arrived: asks about the frame before it (no forecast: yes).
            bool Ask(ShowForecast* forecast, const TickRecord& record, size_t next) {
                if (next == 0) {
                    return true;
                }

                const bool yes = !forecast
                                 || forecast->WillShow(
                                     record,
                                     Stored(in_.frames[next - 1]),
                                     Stored(in_.frames[next])
                                 );

                answer_[next - 1] = yes ? kYes : kNo;
                return yes;
            }

            void Shown(uint64_t serial) {
                const size_t i = indexOf_.at(serial);

                if (!shown_[i] && answer_[i] == kNo) {
                    shownAfterNo_++;
                }

                shown_[i] = true;
            }

            // Over the frames due by the end (`due` of them, the first ones).
            void Print(size_t due) const {
                size_t no = 0, yesNotShown = 0;

                for (size_t i = 0; i < due; i++) {
                    if (answer_[i] == kNo) {
                        no++;
                    } else if (answer_[i] == kYes && !shown_[i]) {
                        yesNotShown++;
                    }
                }

                printf(
                    "%-60s will show: %zu of %zu frames due said no, %zu of those shown after all; %zu "
                    "said yes and never shown\n",
                    "",
                    no,
                    due,
                    shownAfterNo_,
                    yesNotShown
                );
            }

        private:
            enum Answer : char { kNotAsked, kNo, kYes };

            static StoredFrame Stored(const TraceFrame& f) {
                return {true, f.serial, f.arrival, f.smooth, f.slot, 0, true};
            }

            const TraceInput& in_;
            const std::unordered_map<uint64_t, size_t>& indexOf_;
            std::vector<char> answer_;  // per frame of `in_`
            std::vector<bool> shown_;   // per frame of `in_`
            size_t shownAfterNo_ = 0;
        };

        // The ticks of a trace's _ticks.csv, and each shown frame's de-jittered arrival as the plugin had
        // it (`shownSmooth`, to check frames rebuilt from the source's file against).
        bool LoadTicks(const std::string& path, TraceInput* in) {
            FILE* f = nullptr;

            if (fopen_s(&f, path.c_str(), "r") != 0 || !f) {
                return false;
            }

            char line[512];
            while (fgets(line, sizeof(line), f)) {
                unsigned stamp = 0;
                unsigned long long serial = 0;
                double tick = 0, arrival = 0, smooth = 0;
                const int
                    read = sscanf_s(line, "%u,%lf,%llu,%lf,%lf", &stamp, &tick, &serial, &arrival, &smooth);

                if (read >= 2) {
                    in->ticks.push_back(tick);
                }
                if (read == 5) {
                    in->shownSmooth.push_back({serial, smooth});
                }
            }

            fclose(f);
            return !in->ticks.empty();
        }

        // The Pimax source's _pimax.csv: its F rows hold each frame's arrival, de-jittered arrival and
        // slot as the plugin had them.
        bool LoadPimaxFrames(const std::string& path, TraceInput* in) {
            FILE* f = nullptr;

            if (fopen_s(&f, path.c_str(), "r") != 0 || !f) {
                return false;
            }

            char line[512];
            while (fgets(line, sizeof(line), f)) {
                TraceFrame fr;
                unsigned long long serial = 0;
                const int
                    read = sscanf_s(line, "F,%llu,%lf,%lf,%lf", &serial, &fr.arrival, &fr.smooth, &fr.slot);

                if (line[0] == 'F' && read == 4) {
                    fr.serial = serial;
                    in->frames.push_back(fr);
                }
            }

            fclose(f);
            return !in->frames.empty();
        }

        // The OpenXR source's _openxr.csv: each A row is a frame handed to the hosts (serial, arrival),
        // and the F row of the same layer frame says the slot it was drawn for. The file holds no
        // de-jittered arrival or display period, so the source's own arrival timing runs again over the
        // frames, as the source ran it: reset where a new game session starts (the layer's frame numbers
        // start again), and not at a pause. The drawn slots' median step stands in for the period.
        bool LoadOpenXrFrames(const std::string& path, TraceInput* in) {
            FILE* f = nullptr;

            if (fopen_s(&f, path.c_str(), "r") != 0 || !f) {
                return false;
            }

            std::unordered_map<unsigned long long, double> drawnSlot;  // by layer frame
            std::vector<double> slotSteps;
            std::vector<bool> startsSession;  // per frame of `in`
            double lastSlot = 0;
            unsigned long long lastLayerFrame = 0;
            char line[512];

            while (fgets(line, sizeof(line), f)) {
                unsigned long long layerFrame = 0, serial = 0;
                double slot = 0, arrival = 0;

                if (line[0] == 'F' && sscanf_s(line, "F,%llu,%*lf,%lf", &layerFrame, &slot) == 2) {
                    drawnSlot[layerFrame] = slot;
                    if (lastSlot > 0 && slot > lastSlot && slot - lastSlot < 0.05) {
                        slotSteps.push_back(slot - lastSlot);
                    }
                    lastSlot = slot;
                } else if (
                    line[0] == 'A' && sscanf_s(line, "A,%llu,%llu,%lf", &layerFrame, &serial, &arrival) == 3
                ) {
                    const auto drawn = drawnSlot.find(layerFrame);
                    TraceFrame fr;
                    fr.serial = serial;
                    fr.arrival = arrival;
                    fr.slot = drawn != drawnSlot.end() ? drawn->second : 0;  // re-timed below
                    in->frames.push_back(fr);
                    startsSession.push_back(layerFrame < lastLayerFrame);
                    lastLayerFrame = layerFrame;
                }
            }

            fclose(f);

            if (in->frames.empty() || slotSteps.empty()) {
                return false;
            }

            in->period = Median(&slotSteps);
            std::unique_ptr<ArrivalTiming> timing = MakeArrivalTiming(ArrivalTimingKind::kReported);

            for (size_t i = 0; i < in->frames.size(); i++) {
                TraceFrame& fr = in->frames[i];

                if (startsSession[i]) {
                    timing->Reset();
                }

                const ArrivalResult r = timing->Add({fr.arrival, 0, fr.slot, in->period});
                fr.smooth = r.smooth;
                fr.slot = r.slot;
            }

            return true;
        }

        // How far the frames' de-jittered arrivals are from the plugin's own, over the frames its ticks
        // showed.
        void PrintSmoothCheck(const TraceInput& in, const std::unordered_map<uint64_t, size_t>& indexOf) {
            std::vector<double> off;  // per tick that showed a frame (ms)

            for (const auto& shown : in.shownSmooth) {
                const auto i = indexOf.find(shown.first);

                if (i != indexOf.end()) {
                    off.push_back(std::abs(in.frames[i->second].smooth - shown.second) * 1000);
                }
            }

            std::sort(off.begin(), off.end());
            const auto at = [&](double share) {
                return off.empty() ? 0 : off[size_t(share * (off.size() - 1))];
            };
            const auto over = [&](double ms) {
                return off.end() - std::upper_bound(off.begin(), off.end(), ms);
            };

            printf(
                "%s: %zu frames, %zu ticks; display period %.3f ms; de-jittered arrivals off the plugin's, "
                "over "
                "%zu ticks: median %.4f, p99 %.4f, max %.4f ms; %zd over 0.01 ms, %zd over 1 ms\n",
                in.name.c_str(),
                in.frames.size(),
                in.ticks.size(),
                in.period * 1000,
                off.size(),
                at(0.5),
                at(0.99),
                at(1.0),
                over(0.01),
                over(1.0)
            );
        }

        // Frames at `headsetHz` for `seconds` (arrival noise, ~3% dropped, a stall of 20 frames) and
        // OBS's ticks at `hz`, each moved by up to `tickJitterS` either way (0: on the exact grid, as
        // OBS schedules them; findings\pipeline.md); the smoothing changes twice. A pair of runs that
        // differ only in the jitter has the same frames.
        TraceInput Synthetic(int headsetHz, double hz, double tickJitterS, double seconds) {
            TraceInput in;
            char name[80];
            char ticks[24] = "ticks exact";

            if (tickJitterS > 0) {
                snprintf(ticks, sizeof(ticks), "ticks +-%.2f ms", tickJitterS * 1000);
            }

            snprintf(name, sizeof(name), "synthetic: headset %d Hz, OBS %.2f fps, %s", headsetHz, hz, ticks);
            in.name = name;
            in.period = 1.0 / headsetHz;
            in.tickPeriod = 1 / hz;

            // The seed of the 90 Hz runs is what it was before the headset's rate was a parameter.
            uint32_t seed = 777u + static_cast<uint32_t>(hz * 100) + static_cast<uint32_t>(headsetHz - 90);
            auto next = [&] {
                seed = seed * 1664525u + 1013904223u;
                return double(seed >> 8) / double(1u << 24);
            };

            uint64_t serial = 0;
            for (int k = 0; k * in.period < seconds; k++) {
                const double slot = 100 + k * in.period;
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
                const double jitter = tickJitterS > 0 ? (next() - 0.5) * 2 * tickJitterS : 0;
                in.ticks.push_back(100 + i / hz + jitter);
            }

            in.smoothingAt = {{0, 1}, {in.ticks.size() / 3, 0}, {2 * in.ticks.size() / 3, 1}};
            return in;
        }

        // How a run copies the frames into the store: each as it arrives (the Pimax source), each once
        // the next one has arrived (the OpenXR source, which holds a frame to ask the forecast about
        // it), or, held so, only the frames the forecast says a tick shows.
        enum class Copying { kAtArrival, kHeld, kShownOnly };
        constexpr Copying kCopyings[] = {Copying::kAtArrival, Copying::kHeld, Copying::kShownOnly};

        // For a run's first line: nothing for the plugin's copying today.
        const char* NameOf(Copying copying) {
            switch (copying) {
                case Copying::kHeld:
                    return ", each held for the next";
                case Copying::kShownOnly:
                    return ", held, only those shown";
                default:
                    return "";
            }
        }

        // The OpenXR source's copying: each frame held until the next one arrives, or for two display
        // periods if none does (the game paused), and then copied, or not if the caller says so.
        class HeldCopy {
        public:
            HeldCopy(const TraceInput& in, FrameStore* store)
                : in_(in),
                  store_(store),
                  skipped_(in.frames.size(), false) {}

            // Copies the held frame if no next one came within two display periods by `now`.
            void CopyIfLate(double now) {
                const double late = in_.frames[frame_].arrival + 2 * in_.period;

                if (Holding() && late <= now) {
                    Copy(late);
                }
            }

            // Frame `frame` arrived, its copy to take `copy`: copies the held frame now if `copyHeld`,
            // and holds the new one.
            void Next(size_t frame, double copy, bool copyHeld) {
                if (Holding() && copyHeld) {
                    Copy(in_.frames[frame].arrival);
                } else if (Holding()) {
                    skipped_[frame_] = true;
                }

                holding_ = true;
                copied_ = false;
                frame_ = frame;
                copy_ = copy;
            }

            // Stops holding: a frame still held is copied at `now`.
            void Release(double now) {
                if (Holding()) {
                    Copy(now);
                }

                holding_ = false;
            }

            bool Skipped(size_t frame) const { return skipped_[frame]; }
            size_t Copies() const { return copies_; }

        private:
            bool Holding() const { return holding_ && !copied_; }

            void Copy(double start) {
                const TraceFrame& f = in_.frames[frame_];
                store_->Add(f.serial, f.arrival, f.smooth, f.slot, start + copy_);
                copied_ = true;
                copies_++;
            }

            const TraceInput& in_;
            FrameStore* store_;
            std::vector<bool> skipped_;  // per frame of `in_`
            bool holding_ = false, copied_ = false;
            size_t frame_ = 0;  // the frame held
            double copy_ = 0;   // ...and how long its copy takes
            size_t copies_ = 0;
        };

        // Counts the frames due by `tick` (the output delay after their de-jittered arrival) and
        // whether the store still holds each, apart from those never copied.
        void CountDue(
            const TraceInput& in,
            double tick,
            double outDelay,
            const FrameStore& store,
            const HeldCopy& held,
            FrameUse* use
        ) {
            for (; use->nextDue < use->stored; use->nextDue++) {
                const TraceFrame& f = in.frames[use->nextDue];

                if (f.smooth + outDelay > tick) {
                    break;
                }

                if (held.Skipped(use->nextDue)) {
                    use->skipped++;
                } else if (store.Holds(f.serial)) {
                    use->keptUntilDue++;
                } else {
                    use->goneBeforeDue++;
                }
            }
        }

        double Percent(size_t part, size_t whole) {
            return whole > 0 ? 100.0 * static_cast<double>(part) / static_cast<double>(whole) : 0;
        }

        void Run(const TraceInput& in, int smoothing, FramePickerKind kind, Copying copying) {
            auto store = std::make_unique<FrameStore>();
            PipelineConfig config;
            config.framePicker = kind;
            config.smoothing = SmoothingKind(smoothing);
            const bool shownOnly = copying == Copying::kShownOnly;

            std::unique_ptr<FramePicker> picker = MakeFramePicker(config.framePicker);
            picker->Restart(
                SmoothingDelayS(config.smoothing),
                in.period,
                in.tickPeriod,
                kMaxSlots,
                shownOnly
            );
            store->Resize(picker->SlotsInUse());
            double outDelay = picker->OutDelay();  // as last seen: a change is the delay fixed
            const std::unordered_map<uint64_t, size_t> indexOf = IndexBySerial(in);
            WillShowCheck willShow(in, indexOf);
            const std::unique_ptr<ShowForecast> forecast = picker->MakeForecast();
            HeldCopy held(in, store.get());

            uint32_t seed = 4242;
            auto next = [&] {
                seed = seed * 1664525u + 1013904223u;
                return double(seed >> 8) / double(1u << 24);
            };

            size_t frame = 0, change = 0;
            int counts[5] = {}, fixedTick = -1, repeatsNewerDue = 0;
            double fixedDelay = 0, jump = 0, lastTick = 0, lastMoment = 0;
            std::vector<double> behind;  // per pick: the shown image's slot minus the tick's moment (s)
            FrameUse use;

            for (size_t t = 0; t < in.ticks.size(); t++) {
                const double tick = in.ticks[t];
                if (change < in.smoothingAt.size() && in.smoothingAt[change].first == t) {
                    const SmoothingKind k = SmoothingKind(in.smoothingAt[change++].second);
                    if (k != config.smoothing) {
                        config.smoothing = k;
                        picker->Restart(SmoothingDelayS(k), in.period, in.tickPeriod, kMaxSlots, shownOnly);
                        store->Resize(picker->SlotsInUse());
                        outDelay = picker->OutDelay();
                        lastTick = 0;  // the moments move with the smoothing's delay: not a jump
                    }
                }

                // Frames arrived by this tick go into the store; copies take 1-3 ms, 5% of them 5-30 ms.
                for (; frame < in.frames.size() && in.frames[frame].arrival <= tick; frame++) {
                    const double copy = next() < 0.05 ? 0.005 + 0.025 * next() : 0.001 + 0.002 * next();
                    const TraceFrame& f = in.frames[frame];
                    const TickRecord record = picker->Record();

                    // Copied as it arrives: the plugin's copying today, and the OpenXR source's while
                    // the picker doesn't ask it to copy only the frames shown (Instant).
                    if (copying == Copying::kAtArrival || (shownOnly && !record.copiesShownOnly)) {
                        held.Release(f.arrival);
                        store->Add(f.serial, f.arrival, f.smooth, f.slot, f.arrival + copy);
                        use.copies++;
                        willShow.Ask(forecast.get(), record, frame);
                        continue;
                    }

                    held.CopyIfLate(f.arrival);
                    const bool yes = willShow.Ask(forecast.get(), record, frame);
                    held.Next(frame, copy, yes || !shownOnly);
                }

                if (copying != Copying::kAtArrival) {
                    held.CopyIfLate(tick);
                }

                store->Finish(tick);
                use.stored = frame;
                use.mostHolding = std::max(use.mostHolding, store->Holding());
                CountDue(in, tick, picker->OutDelay(), *store, held, &use);
                const Pick p = picker->Tick(tick, in.period, store->slots, kMaxSlots);
                if (picker->OutDelay() != outDelay) {  // the delay fixed
                    outDelay = picker->OutDelay();
                    fixedDelay = outDelay - SmoothingDelayS(config.smoothing);
                    fixedTick = int(t);
                }

                if (picker->SlotsInUse() != store->wantCount) {
                    store->Resize(picker->SlotsInUse());
                }

                counts[int(p.kind)]++;
                if (p.kind == PickKind::kNone) {
                    continue;
                }

                // The tick's moment: how far its view trails the shown image's slot, and how far it moved
                // other than with the ticks (a jump of the whole view).
                const StoredFrame& shown = store->slots[p.index];
                if (p.kind == PickKind::kRepeat
                    && NewerFrameDue(in, indexOf.at(shown.serial), tick, picker->OutDelay())) {
                    repeatsNewerDue++;
                }
                if (shown.serial != use.lastShown) {
                    use.shown++;
                    use.lastShown = shown.serial;
                    willShow.Shown(shown.serial);
                }
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
                "%-60s %s%s, smoothing %d: %zu ticks, %d next, %d repeat (%d with a newer frame due), %d "
                "jump, %d held, %d none; delay fixed at tick %d (%.1f ms), %d entries; view behind its image "
                "%.1f ms (median), moment jumps up to %.1f ms\n",
                in.name.c_str(),
                NameOf(kind),
                NameOf(copying),
                smoothing,
                in.ticks.size(),
                counts[0],
                counts[1],
                repeatsNewerDue,
                counts[2],
                counts[3],
                counts[4],
                fixedTick,
                fixedDelay * 1000,
                picker->SlotsInUse(),
                Median(&behind) * 1000,
                jump * 1000
            );

            if (copying == Copying::kShownOnly) {
                printf(
                    "%-60s frames: %zu arrived, %zu copied, %zu due by the end; of those %.0f%% shown, "
                    "%.0f%% "
                    "not copied, %.0f%% of the copied still stored when due; up to %d entries holding a "
                    "frame\n",
                    "",
                    use.stored,
                    use.copies + held.Copies(),
                    use.nextDue,
                    Percent(use.shown, use.nextDue),
                    Percent(use.skipped, use.nextDue),
                    Percent(use.keptUntilDue, use.nextDue - use.skipped),
                    use.mostHolding
                );
            } else {
                printf(
                    "%-60s frames: %zu stored, %zu due by the end; of those %.0f%% shown, %.0f%% still "
                    "stored "
                    "when due; up to %d entries holding a frame\n",
                    "",
                    use.stored,
                    use.nextDue,
                    Percent(use.shown, use.nextDue),
                    Percent(use.keptUntilDue, use.nextDue),
                    use.mostHolding
                );
            }

            willShow.Print(use.nextDue);
        }

    }

    int RunFramePicker(const std::vector<std::pair<std::string, std::string>>& traces, FramePickerKind kind) {
        for (const auto& t : traces) {
            TraceInput in;
            in.name = t.first.substr(t.first.find_last_of("\\/") + 1);
            const std::string& framesPath = t.second;
            const std::string openXrEnd = "_openxr.csv";
            const bool openXr = framesPath.size() >= openXrEnd.size()
                                && framesPath.compare(
                                       framesPath.size() - openXrEnd.size(),
                                       openXrEnd.size(),
                                       openXrEnd
                                   ) == 0;
            const bool loaded = LoadTicks(t.first, &in)
                                && (openXr ? LoadOpenXrFrames(framesPath, &in)
                                           : LoadPimaxFrames(framesPath, &in));

            if (!loaded) {
                printf("cannot read %s / %s\n", t.first.c_str(), framesPath.c_str());
                return -1;
            }

            // A trace doesn't hold OBS's frame rate: its ticks' median step stands in.
            std::vector<double> steps(in.ticks.size());
            std::adjacent_difference(in.ticks.begin(), in.ticks.end(), steps.begin());
            steps.erase(steps.begin());
            in.tickPeriod = Median(&steps);
            PrintSmoothCheck(in, IndexBySerial(in));

            for (int smoothing = 0; smoothing <= 1; smoothing++) {
                for (Copying copying : kCopyings) {
                    Run(in, smoothing, kind, copying);
                }
            }
        }

        // Each set twice: ticks moved by up to 0.25 ms (a stress test), and on the exact grid.
        for (double tickJitterS : {0.00025, 0.0}) {
            for (double hz : {50.0, 59.94, 60.0, 90.0, 144.0}) {
                for (Copying copying : kCopyings) {
                    Run(Synthetic(90, hz, tickJitterS, 30.0), 1, kind, copying);
                }
            }

            for (Copying copying : kCopyings) {
                Run(Synthetic(144, 60.0, tickJitterS, 30.0), 1, kind, copying);
            }
        }
        return 0;
    }

}
