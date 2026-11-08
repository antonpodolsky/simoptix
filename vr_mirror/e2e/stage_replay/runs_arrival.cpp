// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// stage_replay --stage arrival_timing: the arrival timing (stage 1) over the frame arrivals of OBS
// frame traces (_pimax.csv F rows: arrival and the runtime's next display slot then, and what the
// plugin's own arrival timing gave). Scores the timing noise it removes (its result against the raw
// arrival, rms; and the spacing of the results against the display grid) and how often its slot
// agrees with the one the plugin recorded. Reset after 2 s without a frame, as PimaxSource does when
// it goes idle. Knows nothing about PVR.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "pipeline/pipeline_builder.h"
#include "runs.h"

namespace stage_replay {

    namespace {

        constexpr double kDisplayPeriod = 1.0 / 90;

        struct Arrival {
            double arrival = 0, smooth = 0, slot = 0, currentSlot = 0;
        };

        bool Load(const std::string& path, std::vector<Arrival>* out) {
            FILE* f = nullptr;
            if (fopen_s(&f, path.c_str(), "r") != 0 || !f) {
                return false;
            }
            char line[512];
            while (fgets(line, sizeof(line), f)) {
                Arrival a;
                unsigned long long serial = 0;
                if (line[0] == 'F'
                    && sscanf_s(
                           line,
                           "F,%llu,%lf,%lf,%lf,%lf",
                           &serial,
                           &a.arrival,
                           &a.smooth,
                           &a.slot,
                           &a.currentSlot
                       ) == 5
                    && a.currentSlot > 0) {
                    out->push_back(a);
                }
            }

            fclose(f);
            return !out->empty();
        }

    }

    int RunArrivalTiming(const PipelineConfig& config, const std::vector<std::string>& pimaxTraces) {
        printf(
            "arrival timing %s (lower is better: noise, spacing error; higher is better: slot agreement)\n",
            NameOf(config.arrivalTiming)
        );

        for (const std::string& path : pimaxTraces) {
            std::vector<Arrival> arrivals;
            if (!Load(path, &arrivals)) {
                printf("cannot read %s\n", path.c_str());
                return -1;
            }

            std::unique_ptr<ArrivalTiming> timing = MakeArrivalTiming(config.arrivalTiming);
            double removedSq = 0, spacingSq = 0, recordedSpacingSq = 0;
            size_t agree = 0, spacings = 0, resets = 0;
            double lastArrival = 0, lastSmooth = 0, lastRecorded = 0;

            for (const Arrival& a : arrivals) {
                if (lastArrival > 0 && a.arrival - lastArrival > 2.0) {
                    timing->Reset(), resets++, lastSmooth = 0;
                }

                const ArrivalResult r = timing->Add({a.arrival, a.currentSlot, 0, kDisplayPeriod});
                removedSq += (a.arrival - r.smooth) * (a.arrival - r.smooth);
                agree += std::fabs(r.slot - a.slot) < 0.25 * kDisplayPeriod;

                if (lastSmooth > 0) {
                    // The spacing's distance from a whole number of display periods: what is left of the
                    // noise.
                    const double d = std::remainder(r.smooth - lastSmooth, kDisplayPeriod);
                    const double dr = std::remainder(a.smooth - lastRecorded, kDisplayPeriod);
                    spacingSq += d * d, recordedSpacingSq += dr * dr, spacings++;
                }

                lastArrival = a.arrival, lastSmooth = r.smooth, lastRecorded = a.smooth;
            }

            const size_t n = arrivals.size();
            printf(
                "  %-40s %7zu frames, %zu resets: noise removed %.3f ms rms; spacing off the grid %.4f ms "
                "rms "
                "(the plugin's own %.4f); slot agrees with the plugin's on %.2f%%\n",
                path.substr(path.find_last_of("\\/") + 1).c_str(),
                n,
                resets,
                std::sqrt(removedSq / n) * 1000,
                std::sqrt(spacingSq / std::max<size_t>(1, spacings)) * 1000,
                std::sqrt(recordedSpacingSq / std::max<size_t>(1, spacings)) * 1000,
                100.0 * agree / n
            );
        }

        return 0;
    }

}
