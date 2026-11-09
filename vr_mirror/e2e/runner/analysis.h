// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The measures: the tool's frame trace, captured views matched to the frames they show, and every
// quantity a scenario is graded on (frame rate, jitter, stalls, lag, mismatch, wrong poses), plus
// the result lines. Knows nothing about running the tool or capturing.
#pragma once

#include "capture.h"
#include "common.h"
#include "fake_log.h"

// The tool's own frames (from SIMOPTIX_VR_MIRROR_TRACE), one per output frame drawn (one per game frame):
// the game frame shown, when it was presented, the head pose its picture was re-projected from (raw)
// and the view it meant to show.
struct ToolTrace {
    std::vector<uint64_t> serial;   // the game frame shown (a repeated frame shows again)
    std::vector<double> arrival;    // when the tool saw that game frame
    std::vector<double> slot;       // the display slot it arrived for
    std::vector<double> drawnSlot;  // ...and the one its source says it was drawn for (0: it doesn't say)
    std::vector<double> presented;  // when it finished presenting the output frame
    std::vector<Quat> raw, view;
    std::vector<float> zoom;   // zoom used for each frame
    unsigned edgeClamps = 0;   // frames where the tool's last-resort edge limit acted
    unsigned edgePlanned = 0;  // frames the look-ahead's planned edge correction moved

    // Columns by their header name, so builds that write other columns read the same (older builds
    // wrote lags and the cockpit correction between them; the counters are cumulative). The pose the
    // picture was warped from: `img` (the observer's trace), else `raw` (older builds). Lines that
    // aren't frames ("#stats,...") are skipped.
    bool Load(const fs::path& path) {
        std::ifstream f(path);
        std::string line;
        std::getline(f, line);  // header
        std::vector<std::string> names;
        for (size_t start = 0; start <= line.size();) {
            size_t end = line.find(',', start);
            if (end == std::string::npos) {
                end = line.size();
            }
            names.push_back(line.substr(start, end - start));
            start = end + 1;
        }

        auto column = [&](const char* name) {
            for (size_t i = 0; i < names.size(); i++) {
                if (names[i] == name) {
                    return int(i);
                }
            }

            return -1;
        };

        const int cSerial = column("serial"), cArrival = column("arrival"), cPresented = column("presented");
        const int cSlot = column("slot"), cDrawn = column("drawn_slot");
        const int cRaw = column("img_x") >= 0 ? column("img_x") : column("raw_x");
        const int cView = column("view_x"), cZoom = column("zoom");
        const int cClamps = column("edge_clamps"), cPlanned = column("edge_planned");

        if (cSerial < 0 || cArrival < 0 || cPresented < 0 || cRaw < 0 || cView < 0) {
            return false;
        }

        std::vector<double> v;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }
            v.clear();
            for (const char* p = line.c_str(); *p;) {
                char* end = nullptr;
                v.push_back(std::strtod(p, &end));
                p = end;
                while (*p && *p != ',') {
                    p++;
                }
                if (*p == ',') {
                    p++;
                }
            }

            auto at = [&](int c) { return c >= 0 && c < int(v.size()) ? v[size_t(c)] : 0.0; };
            if (int(v.size()) < cView + 4) {
                continue;
            }
            serial.push_back(uint64_t(at(cSerial)));
            arrival.push_back(at(cArrival));
            slot.push_back(at(cSlot));
            drawnSlot.push_back(at(cDrawn));
            presented.push_back(at(cPresented));
            raw.push_back({float(at(cRaw)), float(at(cRaw + 1)), float(at(cRaw + 2)), float(at(cRaw + 3))});
            view.push_back(
                {float(at(cView)), float(at(cView + 1)), float(at(cView + 2)), float(at(cView + 3))}
            );

            zoom.push_back(float(at(cZoom)));
            if (cClamps >= 0) {
                edgeClamps = unsigned(at(cClamps));
            }
            if (cPlanned >= 0) {
                edgePlanned = unsigned(at(cPlanned));
            }
        }

        return presented.size() > 10;
    }

    // Index of the newest present at or before t, or -1.
    int PresentBefore(double t) const {
        auto it = std::upper_bound(presented.begin(), presented.end(), t);
        return it == presented.begin() ? -1 : int(it - presented.begin()) - 1;
    }
};

// A view direction over time.
struct Series {
    std::vector<double> t;
    std::vector<Vec3> dir;
};

// Captured images, each matched to the tool frame it shows: among the presents just before the
// capture (the screen updates up to a refresh later), the one whose intended view is closest.
struct Matched {
    Series seen, intended, eye;
    std::vector<double> roll;
};

struct Result {
    std::string name;
    bool pass = true;
    std::vector<std::string> lines;
    std::map<std::string, double> values;  // every measure by its name in the lines (Check)
};

// What the tool meant to show at each of its frames (after the first second), and where the
// eye pointed at those moments: at presented - `delay`: how long after its own moment the tool presents a
// frame beyond the usual few ms (its look-ahead delay, lookahead_smoothing.h; 0 for Instant).
void IntendedSeries(const ToolTrace& tt, const FakeLog& log, double delay, Series* intended, Series* eye);
Matched MatchCaptures(
    const std::vector<ViewSample>& caps,
    const FakeLog& log,
    const ToolTrace& tt,
    double delay,
    double* validPct
);

// Like a 60 fps OBS recording: each tick shows the newest captured frame. Returns the share
// of ticks that repeat the previous image while the head is moving (visible as stutter).
double ObsRepeatPct(const std::vector<ViewSample>& caps, const FakeLog& log);
// Rate and longest gap of the tool's presents, skipping the first second (startup).
void ToolPresentStats(const ToolTrace& tt, double* fps, double* maxGapMs);
// How unevenly the tool's presents are spaced (rms ms of each step's distance from a whole number
// of 90 Hz frames), skipping the first second.
double PresentJitterMs(const ToolTrace& tt);
// How long after its arrival the tool presented each game frame, skipping the first second: the
// median (ms) and its spread (99th minus 1st percentile, ms). With Look-ahead it is the
// picture's delay, which audio sync shifts the sound by, so it should hold steady.
void PresentDelayStats(const ToolTrace& tt, double* medianMs, double* spreadMs);
// Game frames (after the first second) that the tool never presented.
int MissedGameFrames(const FakeLog& log, const ToolTrace& tt, int* considered);
// The game's frames, from its first, before the first one the tool showed (all of them if none).
int MissedAtStart(const FakeLog& log, const ToolTrace& tt);
// Of the tool's frames after the first second: the share (%) showing the same game frame as the
// one before (a tick repeating it), and the share that moved past a game frame the game delivered
// without showing it (skipping it). 0 and the missed frames when the tool draws once per game frame.
void RepeatSkipPct(const FakeLog& log, const ToolTrace& tt, double* repeatPct, double* skipPct);
// Per game frame the tool showed (matched by arrival time; each once, at its first output frame):
// degrees between the head pose the tool re-projected it from and the pose the fake game rendered it
// with. Exact in replays, where the fake renders with the runtime's recorded predictions, which are
// also what the tool looks up.
std::vector<double> PoseErrorsDeg(const FakeLog& log, const ToolTrace& tt);
// ...as rotations (from the pose the game rendered with to the one the tool used), in order.
std::vector<Quat> PoseErrors(const FakeLog& log, const ToolTrace& tt);
// Visible jumps: degrees the pose error changed from each game frame shown to the next (the
// rotation between their errors). A steady error moves the whole picture with the head and isn't
// seen; a change shows as the picture jumping for a frame.
std::vector<double> PoseJumpsDeg(const FakeLog& log, const ToolTrace& tt);
// A jump that shows: 0.1 deg is 1.8 px of a 1920x1080 recording at zoom 1.3 (17.9 px per degree in
// the middle), where the shake study counted jumps over 2 px.
constexpr double kVisibleJumpDeg = 0.1;
// Drift: the largest mean, over any run of `window` consecutive values, of rotation vectors `v`
// (degrees): what stays when the frame-to-frame changes average out.
double LargestMeanDeg(const std::vector<Vec3>& v, int window);
// The pose errors (PoseErrors),
// per game frame shown (each once, at its first output frame), as rotation vectors in degrees.
std::vector<Vec3> PoseErrorVectorsDeg(const FakeLog& log, const ToolTrace& tt);
// Frames among those whose pose error is over 0.02 deg.
int WrongPoseFrames(const FakeLog& log, const ToolTrace& tt, int* considered);
// What a 60 fps recording of the tool's window shows, from the trace (exact, no color decoding): at
// each tick (from a second in, plus `phase` of a tick), the view of the newest frame presented
// before it. A frame held over because the game dropped one, or presented late, shows as it would
// in a recording.
Series RecordedSeries(const ToolTrace& tt, const Quat& outputAxis, double fps, double phase);
// Its jitter (JitterDeg), averaged over evenly spread phases: where a recording's ticks fall
// against the tool's frames is chance, and with dropped frames it moved one run's value by 15%.
double RecordedJitterDeg(const ToolTrace& tt, const Quat& outputAxis, double fps);
// Jitter: what's left after fitting a smooth curve (a local quadratic over +-50 ms), rms degrees.
double JitterDeg(const Series& s);
// View stalling mid-turn and then catching up.
int CountStalls(const Series& view, const Series& eye);
// Stabilization off: the view should match the eye up to a small constant delay.
double TrackErrorDeg(const Series& seen, const FakeLog& log, double* bestDelayMs);
// Adds a result line; fails the result if a limit (>= 0) is set and not met.
void Check(
    Result& r,
    const char* what,
    double value,
    double limit,
    const char* unit,
    bool lessIsBetter = true
);

// One row per captured tool frame: what was seen, what the tool intended, where the eye was.
void WriteTrackCsv(const fs::path& path, const Matched& m);
