// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "compare.h"

namespace {

    // A measure the look-ahead may not make worse. A variant fails it when it is worse than Instant by
    // more than rel * Instant + abs: room for the run-to-run spread of the same build, measured over
    // repeated runs (vr_mirror\e2e\README.md, "Smoothing compared with Instant").
    struct Rule {
        const char* measure;  // its name in the result lines (Check)
        double rel, abs;
        bool lessIsBetter;
        bool view;  // a view or smoothing measure (Scenario::compareView)
        // Never a fail while this measure of the variant stays at or under `floor` (a ratio of jitter no
        // one can see: Instant's dead zone holds a slow view perfectly still, so it reads 0 where the
        // look-ahead leaves a few thousandths of a degree).
        const char* floorMeasure;
        double floor;
    };

    // head_still's limit: jitter under a tenth of a pixel. A view jitter (deg rms) may be that much
    // above Instant's: where Instant's dead zone holds a slow view still, the look-ahead follows the
    // slow move, and a recording shows its judder (replay_false_retiming_short_hiccup: 0.021 against
    // 0.016 deg).
    constexpr double kInvisibleJitterDeg = 0.005;

    const Rule kGraded[] = {
        {"game frames the tool missed", 0, 0.5, true, false, nullptr, 0},
        {"first game frames missed", 0, 0, true, false, nullptr, 0},
        {"smoothed view jitter", 0, kInvisibleJitterDeg, true, true, nullptr, 0},  // deg rms
        {"jitter ratio smoothed/head", 0, 0.005, true, true, "smoothed view jitter", kInvisibleJitterDeg},
        {"stalls mid-turn", 0, 0, true, true, nullptr, 0},
        // queued_late_frames reads 6.9-9.5% at Instant alone
        {"frames from the wrong pose", 0.4, 0.3, true, false, nullptr, 0},
        // what a 60 fps recording shows: dropped frames held over, present timing, the 90-to-60 judder
        {"recorded view jitter", 0.1, kInvisibleJitterDeg, true, true, nullptr, 0},
        {"max horizon tilt", 0, 0.05, true, true, nullptr, 0},
        {"frames with black borders", 0, 0, true, true, nullptr, 0},
    };

    // Shown side by side, not graded: they differ by design (the delay) or are informational. The tool's
    // frame rate follows the game's, which varies between runs (a fake game stalling for 144 ms took
    // one run to 87.2 fps): the frames the tool missed say whether it kept up.
    const char* const kShown[] = {
        "tool frames/s",
        "game frames/s",
        "arrival to present",
        "  its spread",
        "longest tool frame gap",
        "60 fps OBS repeats (moving)",
        "max lag behind head",
        "image/pose mismatch",
        "view jitter (on screen)",
        "jitter ratio view/head",
        "pose error",
        "edge limit acted",
        "runtime calls/s while no game ran",
    };

    const char* const kColumn[] = {"Instant", "Smooth", "Smoothest"};

    std::string Row(const char* measure, const Result* at[3], const char* verdict) {
        char buf[256];
        int n = snprintf(buf, sizeof(buf), "  %-34s", measure);
        for (int k = 0; k < 3; k++) {
            auto it = at[k]->values.find(measure);
            if (it == at[k]->values.end()) {
                n += snprintf(buf + n, sizeof(buf) - n, " %10s", "-");
            } else {
                n += snprintf(buf + n, sizeof(buf) - n, " %10.3f", it->second);
            }
        }

        snprintf(buf + n, sizeof(buf) - n, "  %s", verdict);
        return buf;
    }

}

std::vector<Result> CompareSmoothings(const std::vector<Scenario>& runs, const std::vector<Result>& results) {
    // The bases that ran at all three smoothings, in the order they ran.
    std::vector<std::string> bases;
    std::map<std::string, const Result* [3]> found;
    std::map<std::string, const Scenario*> instant;
    for (size_t i = 0; i < runs.size(); i++) {
        const Scenario& sc = runs[i];
        if (sc.base.empty()) {
            continue;
        }
        if (!found.count(sc.base)) {
            bases.push_back(sc.base);
            for (auto& p : found[sc.base]) {
                p = nullptr;
            }
        }

        found[sc.base][sc.smoothing] = &results[i];
        if (sc.smoothing == 0) {
            instant[sc.base] = &sc;
        }
    }

    std::vector<Result> out;
    for (const auto& base : bases) {
        const Result** at = found[base];
        if (!at[0] || !at[1] || !at[2]) {
            continue;
        }

        Result c;
        c.name = base;
        char head[256];
        snprintf(head, sizeof(head), "  %-34s %10s %10s %10s", "", kColumn[0], kColumn[1], kColumn[2]);
        c.lines.push_back(head);
        const Scenario& sc = *instant[base];

        for (const Rule& rule : kGraded) {
            if (rule.view && !sc.compareView) {
                continue;
            }
            auto inst = at[0]->values.find(rule.measure);
            if (inst == at[0]->values.end()) {
                continue;
            }
            const double allowed = rule.lessIsBetter ? inst->second * (1 + rule.rel) + rule.abs
                                                     : inst->second * (1 - rule.rel) - rule.abs;

            std::string worse;
            for (int k = 1; k < 3; k++) {
                auto v = at[k]->values.find(rule.measure);
                if (v == at[k]->values.end()) {
                    continue;
                }
                if (rule.floorMeasure) {
                    auto fl = at[k]->values.find(rule.floorMeasure);
                    if (fl != at[k]->values.end() && fl->second <= rule.floor) {
                        continue;
                    }
                }

                if (rule.lessIsBetter ? v->second > allowed : v->second < allowed) {
                    worse += worse.empty() ? kColumn[k] : std::string(", ") + kColumn[k];
                }
            }

            char verdict[128];
            if (worse.empty()) {
                snprintf(verdict, sizeof(verdict), "ok");
            } else {
                snprintf(
                    verdict,
                    sizeof(verdict),
                    "FAIL: %s worse than Instant (allowed %s %.3f)",
                    worse.c_str(),
                    rule.lessIsBetter ? "<=" : ">=",
                    allowed
                );
            }

            if (!worse.empty()) {
                c.pass = false;
            }
            c.lines.push_back(Row(rule.measure, at, verdict));
        }

        for (const char* measure : kShown) {
            if (at[0]->values.count(measure)) {
                c.lines.push_back(Row(measure, at, ""));
            }
        }

        out.push_back(c);
    }

    return out;
}
