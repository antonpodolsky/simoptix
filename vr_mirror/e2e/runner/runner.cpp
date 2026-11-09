// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// End-to-end test runner for simoptix_vr_mirror.exe.
//
// For each scenario: runs a copy of the real simoptix_vr_mirror.exe against the fake PVR runtime
// (vr_mirror/e2e/fake_pvr), or, with frame_source=openxr, against the fake game with the OpenXR
// layer's frames (vr_mirror/e2e/fake_openxr), captures its output window with Windows Graphics Capture (the
// method OBS uses for "Windows 10 (1903 and up)" window capture), samples it like a 60 fps OBS recording, and
// reads from the pixel colors where the stabilized view points in every frame. That view track is then
// compared with the true head motion from the fake's log.
//
// Usage: e2e_runner.exe [scenario ...]   (no arguments: run all; --list: list scenarios)

#include "analysis.h"
#include "capture.h"
#include "common.h"
#include "compare.h"
#include "fake_log.h"
#include "pipeline/4_smoothing/lookahead_smoothing.h"
#include "scenarios.h"
#include "tool_process.h"

namespace {

    bool g_foreground = true;  // keep the tool window on top (unoccluded); --background turns this off
    std::string g_outputSize;  // --output WxH: tool window size for every scenario
    fs::path g_tool;           // --tool <exe>: test this build instead of mirror_app\simoptix_vr_mirror.exe
    std::map<std::string, std::string> g_settings;  // --set key=value: tool setting for every scenario
    std::map<std::string, std::string> g_fake;  // --fake key=value: fake runtime setting for every scenario
                                                // (a scenario's own settings take precedence)

    // Command-line text to a std::string (the arguments are plain ASCII: names, numbers, paths aren't
    // passed this way).
    std::string Narrow(const std::wstring& w) {
        std::string s;
        for (wchar_t c : w) {
            s += char(c < 128 ? c : '?');
        }
        return s;
    }

    // A scenario's folder: a private copy of the real tool next to the fake runtime, and the tool's ini.
    fs::path PrepareFolder(const Scenario& sc, const fs::path& root) {
        fs::path dir = root / "e2e" / "out" / sc.name;
        fs::remove_all(dir);
        fs::create_directories(dir);

        // A private copy of the real tool next to the fake runtime. A distinct exe name keeps the
        // tool's handle lookup away from any other simoptix_vr_mirror.exe that may be running.
        fs::path exe = dir / "simoptix_vr_mirror_e2e.exe";
        fs::copy_file(g_tool.empty() ? root / "mirror_app" / "simoptix_vr_mirror.exe" : g_tool, exe);
        fs::copy_file(root / "e2e" / "bin" / "libPVRClient64.dll", dir / "libPVRClient64.dll");

        std::ofstream(dir / "simoptix_vr_mirror.ini") << [&] {
            std::ostringstream ini;
            ini << "[mirror]\n";
            for (const auto& kv : sc.settings) {
                ini << kv.first << "=" << kv.second << "\n";
            }
            for (const auto& kv : g_settings) {
                if (!sc.settings.count(kv.first)) {
                    ini << kv.first << "=" << kv.second << "\n";
                }
            }

            const std::string& size = g_outputSize.empty() ? sc.output : g_outputSize;
            const size_t x = size.find('x');
            if (x != std::string::npos) {
                ini << "output_width=" << size.substr(0, x) << "\noutput_height=" << size.substr(x + 1)
                    << "\n";
            }

            return ini.str();
        }();

        return dir;
    }

    // The tool window's width / height (--output, else the scenario's size).
    double OutputAspect(const Scenario& sc) {
        const std::string& size = g_outputSize.empty() ? sc.output : g_outputSize;
        const size_t x = size.find('x');
        const double width = std::atof(size.substr(0, x).c_str());
        const double height = x != std::string::npos ? std::atof(size.substr(x + 1).c_str()) : 0;
        return height > 0 ? width / height : 16.0 / 9.0;
    }

    bool UsesOpenXr(const Scenario& sc) {
        const auto source = sc.settings.find("frame_source");
        return source != sc.settings.end() && source->second == "openxr";
    }

    // Runs the tool (and, for OpenXR, the fake game) for the scenario's time while its window is
    // captured. False, with the reason in the result, when something didn't start.
    bool Record(
        const Scenario& sc,
        const fs::path& root,
        const fs::path& dir,
        std::vector<ViewSample>* caps,
        Result& r
    ) {
        const fs::path exe = dir / "simoptix_vr_mirror_e2e.exe";
        const fs::path scenarioFile = dir / "scenario.cfg", logFile = dir / "fake.log";

        // frame_source=openxr: the frames come from the fake game with the OpenXR layer's frames, a process
        // of its own beside the tool, which stops when this event is set.
        const bool openxr = UsesOpenXr(sc);
        const std::wstring stopEvent = L"Local\\SimOptix.FakeOpenXR.Stop."
                                       + std::to_wstring(GetCurrentProcessId());

        {
            std::ofstream cfg(scenarioFile);
            cfg << "motion=" << sc.motion << "\nlog=" << logFile.string() << "\n";
            if (openxr) {
                cfg << "stop_event=" << Narrow(stopEvent) << "\n";
            }
            for (const auto& kv : sc.fake) {
                if (!g_fake.count(kv.first)) {
                    cfg << kv.first << "=" << kv.second << "\n";
                }
            }

            for (const auto& kv : g_fake) {
                cfg << kv.first << "=" << kv.second << "\n";
            }
        }

        FakeGameProcess game;
        if (openxr
            && !LaunchFakeGame(
                dir,
                root / "e2e" / "bin" / "fake_openxr.exe",
                scenarioFile,
                stopEvent,
                &game
            )) {
            r.pass = false;
            r.lines.push_back("  the fake game (fake_openxr.exe) did not start");
            StopFakeGame(game);
            return false;
        }

        ToolProcess proc;
        if (!Launch(dir, exe, scenarioFile, &proc)) {
            r.pass = false;
            r.lines.push_back("  tool did not start or show its window (see tool.log)");
            if (proc.pi.hProcess) {
                Close(proc);
            }
            StopFakeGame(game);
            return false;
        }

        if (g_foreground) {
            SetWindowPos(proc.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        }

        Sleep(1500);  // let the filter settle and the window show real frames

        // Never destroyed: Windows can still be inside a capture callback after Stop(), and one
        // small object per scenario is cheaper than racing that.
        WindowCapture& cap = *new WindowCapture();
        if (!cap.Start(proc.hwnd)) {
            r.pass = false;
            r.lines.push_back("  window capture failed to start");
            Close(proc);
            StopFakeGame(game);
            return false;
        }

        Sleep(DWORD(sc.seconds * 1000));
        cap.Stop();
        Close(proc);
        StopFakeGame(game);
        *caps = cap.Samples();
        return true;
    }

    // What a run left to grade: the fake's log, the tool's trace, the captures and the view tracks matched
    // from them.
    struct Measured {
        bool openxr = false;
        FakeLog log;
        ToolTrace tools;
        std::vector<ViewSample> caps;
        Matched m;
        Series intended, eye;
    };

    // Loads the run's log and trace and matches them with the captures. False, with the reason in the
    // result, when one is missing or there are too few frames.
    bool Measure(const Scenario& sc, const fs::path& dir, Measured* d, Result& r) {
        if (!d->log.Load(dir / "fake.log")) {
            r.pass = false;
            r.lines.push_back("  fake runtime log missing or empty");
            return false;
        }

        double validPct = 0;
        {
            std::ofstream f(dir / "captures.csv");
            f << "t,received,valid,no_code\n";  // no_code: bit per Patch (patches.h) showing no direction
                                                // code
            f.precision(9);
            for (const auto& c : d->caps) {
                f << std::fixed << c.t << "," << c.received << "," << (c.valid ? 1 : 0) << "," << c.noCode
                  << "\n";
            }
        }

        if (!d->tools.Load(dir / "tool_frames.csv")) {
            r.pass = false;
            r.lines.push_back("  tool frame trace missing (SIMOPTIX_VR_MIRROR_TRACE)");
            return false;
        }

        // The tool's look-ahead delay (the scenario's smoothing): it presents each frame that long after
        // its own moment, so the head is taken at presented - delay.
        // Frames are matched between the fake's log and the tool's trace by when they arrived; the OpenXR
        // layer's by the display slot they were drawn for, which both know exactly (the tool sees them a
        // frame or two after the game ended them, when the layer has seen their copies finished).
        if (d->openxr) {
            d->tools.arrival = d->tools.drawnSlot;
            d->log.arrivals = d->log.targets;
        }

        const double delay = sc.smoothing > 0 ? LookaheadOutputDelayS() : 0;
        d->m = MatchCaptures(d->caps, d->log, d->tools, delay, &validPct);
        IntendedSeries(d->tools, d->log, delay, &d->intended, &d->eye);
        r.lines.push_back("  frames");
        if (sc.viewOnScreen) {
            Check(r, "decodable captures", validPct, sc.limits.minValidPct, "%", false);
        }
        if ((sc.viewOnScreen && d->m.seen.t.size() < 60) || d->intended.t.size() < 60) {
            r.pass = false;
            r.lines.push_back("  too few frames to analyze");
            return false;
        }

        if (sc.viewOnScreen) {
            WriteTrackCsv(dir / "track.csv", d->m);
        }
        return true;
    }

    void CheckFrames(const Scenario& sc, const Measured& d, Result& r) {
        const FakeLog& log = d.log;
        const ToolTrace& tools = d.tools;
        const std::vector<ViewSample>& caps = d.caps;
        const double gameFps = log.arrivals.size() > 1
                                   ? (log.arrivals.size() - 1) / (log.arrivals.back() - log.arrivals.front())
                                   : 0;

        double toolFps = 0, toolGap = 0;
        ToolPresentStats(tools, &toolFps, &toolGap);
        int considered = 0, missed = MissedGameFrames(log, tools, &considered);
        const double capturedFps = caps.size() > 1 ? (caps.size() - 1) / (caps.back().t - caps.front().t) : 0;
        Check(r, "game frames/s", gameFps, -1, "fps");
        Check(r, "tool frames/s", toolFps, sc.limits.minToolFps, "fps", false);
        Check(
            r,
            "game frames the tool missed",
            considered ? 100.0 * missed / considered : 0,
            sc.limits.maxMissedPct,
            "%"
        );

        double repeatPct = 0, skipPct = 0;
        RepeatSkipPct(log, tools, &repeatPct, &skipPct);
        Check(r, "tool frames repeating one", repeatPct, -1, "% (ticks showing the game frame before again)");
        Check(r, "tool frames skipping one", skipPct, -1, "% (moved past a game frame without showing it)");
        Check(r, "longest tool frame gap", toolGap, sc.limits.maxToolGapMs, "ms");
        Check(r, "tool present jitter", PresentJitterMs(tools), -1, "ms rms (lower = fewer OBS repeats)");

        double delayMs = 0, delaySpreadMs = 0;
        PresentDelayStats(tools, &delayMs, &delaySpreadMs);
        Check(r, "arrival to present", delayMs, -1, "ms (median)");
        Check(r, "  its spread", delaySpreadMs, -1, "ms (1st to 99th percentile)");
        Check(r, "captured frames/s", capturedFps, -1, "fps (window capture)");

        if (sc.viewOnScreen) {
            Check(r, "60 fps OBS repeats (moving)", ObsRepeatPct(caps, log), sc.limits.maxObsRepeatPct, "%");
        }
    }

    // What the tool does before the game starts, for scenarios that start the game late.
    void CheckBeforeGame(const Scenario& sc, const Measured& d, Result& r) {
        r.lines.push_back("  before the game started");
        if (d.openxr) {
            // No runtime to call: the tool only looks for the layer's memory once a second meanwhile.
        } else if (d.log.idleCalls < 0) {
            r.pass = false;
            r.lines.push_back("  the fake logged no idle calls (did the game start?)");
        } else {
            Check(
                r,
                "runtime calls/s while no game ran",
                d.log.idleCalls / std::max(1e-9, d.log.idleSeconds),
                sc.limits.maxIdleCallsPerSec,
                "/s"
            );
        }

        Check(r, "first game frames missed", MissedAtStart(d.log, d.tools), sc.limits.maxStartMissed, "");
    }

    // The smoothing itself, from the tool's intended view at every frame (90 Hz, exact times).
    void CheckSmoothing(const Scenario& sc, const Measured& d, Result& r) {
        const Series& intended = d.intended;
        const Series& eye = d.eye;
        r.lines.push_back("  smoothing (tool's intended view)");
        double maxLag = 0;
        for (size_t i = 0; i < intended.t.size(); i++) {
            maxLag = std::max(maxLag, AngleDeg(intended.dir[i], eye.dir[i]));
        }

        double eyeJitter = JitterDeg(eye), intendedJitter = JitterDeg(intended);
        Check(r, "head jitter (input)", eyeJitter, -1, "deg rms");
        Check(r, "smoothed view jitter", intendedJitter, sc.limits.maxSmoothedJitterDeg, "deg rms");
        Check(
            r,
            "jitter ratio smoothed/head",
            eyeJitter > 1e-6 ? intendedJitter / eyeJitter : 0,
            sc.limits.maxSmoothedRatio,
            ""
        );

        Check(r, "stalls mid-turn", CountStalls(intended, eye), sc.limits.maxStalls, "");

        // As a 60 fps recording shows it: held frames (drops) and the 90-to-60 fps judder included.
        Check(
            r,
            "recorded view jitter",
            RecordedJitterDeg(d.tools, d.log.outputAxis, 60),
            sc.limits.maxRecordedJitterDeg,
            "deg rms (60 fps)"
        );

        Check(r, "max lag behind head", maxLag, sc.limits.maxLagDeg, "deg");
    }

    // Exactly which pose each game frame was warped from, against the one it was rendered with.
    void CheckPoses(const Scenario& sc, const Measured& d, Result& r) {
        const FakeLog& log = d.log;
        const ToolTrace& tools = d.tools;
        r.lines.push_back(sc.fake.count("replay") ? "  poses (replay)" : "  poses");
        const std::vector<double> errors = PoseErrorsDeg(log, tools);
        double sumSq = 0, worst = 0;
        for (double e : errors) {
            sumSq += e * e, worst = std::max(worst, e);
        }
        int considered = 0, wrong = WrongPoseFrames(log, tools, &considered);
        Check(
            r,
            "frames from the wrong pose",
            considered ? 100.0 * wrong / considered : 0,
            sc.limits.maxWrongPosePct,
            "%"
        );

        Check(
            r,
            "pose error",
            errors.empty() ? 0 : std::sqrt(sumSq / errors.size()),
            sc.limits.maxPoseErrorRmsDeg,
            "deg rms"
        );

        Check(r, "  worst frame's pose", worst, -1, "deg");
        const std::vector<double> jumps = PoseJumpsDeg(log, tools);
        const double visible = double(std::count_if(jumps.begin(), jumps.end(), [](double j) {
            return j > kVisibleJumpDeg;
        }));

        Check(
            r,
            "visible pose jumps",
            jumps.empty() ? 0 : 100.0 * visible / jumps.size(),
            sc.limits.maxPoseJumpPct,
            "% of frames (the error changing over 0.1 deg; lower is better)"
        );

        Check(
            r,
            "  largest jump",
            jumps.empty() ? 0 : *std::max_element(jumps.begin(), jumps.end()),
            -1,
            "deg"
        );

        // Drift: a drawn pose that corrects poses from the picture (the cockpit correction) follows
        // the picture from frame to frame and the head poses in the long run, so over the whole run
        // its error averages out; one that drifts doesn't (offline, with no pull back: 3.4 deg). Was the
        // correction's own mean (0.08-0.10 deg), which no longer leaves the drawn pose;
        // the pose error's mean read 0.29-0.31 deg there, 0.19 with the slot check alone. Over a second
        // it may hold a slot error.
        const std::vector<Vec3> errorVectors = PoseErrorVectorsDeg(log, tools);
        Check(
            r,
            "pose drift",
            LargestMeanDeg(errorVectors, int(errorVectors.size())),
            sc.limits.maxPoseDriftDeg,
            "deg (the pose error's mean over the run; lower is better)"
        );

        Check(
            r,
            "slow pose error",
            LargestMeanDeg(errorVectors, 90),
            -1,
            "deg (largest 1 s mean of the pose error; lower is better)"
        );
    }

    // What actually ends up on screen: the intended view plus the error from the game having
    // drawn the frame with a different head pose than the tool corrected with.
    void CheckOnScreen(const Scenario& sc, const Measured& d, Result& r) {
        const Matched& m = d.m;
        r.lines.push_back("  on screen (captured)");
        if (!sc.viewOnScreen) {
            return;
        }

        // A camera turned sideways from the head tilts as the head pitches unless it is levelled itself,
        // so the tilt is also graded where the view looks well up or down.
        constexpr double kPitchedDeg = 15;
        double sumSq = 0, maxMismatch = 0, maxRoll = 0, maxPitchedRoll = 0;

        for (size_t i = 0; i < m.seen.t.size(); i++) {
            const double a = AngleDeg(m.seen.dir[i], m.intended.dir[i]);
            const double roll = std::fabs(m.roll[i]);
            sumSq += a * a;
            maxMismatch = std::max(maxMismatch, a);
            maxRoll = std::max(maxRoll, roll);

            if (std::fabs(PitchDeg(m.seen.dir[i])) > kPitchedDeg) {
                maxPitchedRoll = std::max(maxPitchedRoll, roll);
            }
        }

        double seenJitter = JitterDeg(m.seen), seenEyeJitter = JitterDeg(m.eye);
        Check(
            r,
            "image/pose mismatch",
            std::sqrt(sumSq / m.seen.t.size()),
            sc.limits.maxMismatchDeg,
            "deg rms"
        );
        Check(r, "  worst frame", maxMismatch, sc.limits.maxWorstMismatchDeg, "deg");
        Check(r, "view jitter (on screen)", seenJitter, sc.limits.maxJitterDeg, "deg rms");
        Check(
            r,
            "jitter ratio view/head",
            seenEyeJitter > 1e-6 ? seenJitter / seenEyeJitter : 0,
            sc.limits.maxJitterRatio,
            ""
        );

        Check(r, "max horizon tilt", maxRoll, sc.limits.maxRollDeg, "deg");
        Check(
            r,
            "  looking up or down",
            maxPitchedRoll,
            sc.limits.maxPitchedRollDeg,
            "deg (pitch over 15 deg)"
        );
    }

    // The view's width on screen against the one its zoom asks for: the image's half-angle on each side
    // of its middle, over the zoom (FakeLog::QuarterSpanDeg). The direction it looks is graded with the
    // image/pose mismatch, which compares it with the middle of the image.
    void CheckViewWidth(const Scenario& sc, const Measured& d, Result& r) {
        std::vector<double> spans;
        for (const auto& c : d.caps) {
            if (c.valid) {
                spans.push_back(c.quarterSpanDeg);
            }
        }

        if (spans.empty() || d.tools.zoom.empty()) {
            return;
        }

        std::vector<float> zooms = d.tools.zoom;
        std::nth_element(spans.begin(), spans.begin() + spans.size() / 2, spans.end());
        std::nth_element(zooms.begin(), zooms.begin() + zooms.size() / 2, zooms.end());
        const double seen = spans[spans.size() / 2];
        const double asked = d.log.QuarterSpanDeg(zooms[zooms.size() / 2], OutputAspect(sc));

        Check(r, "view width (between the quarter patches)", seen, -1, "deg (median)");
        Check(r, "  off its zoom", std::fabs(seen - asked), sc.limits.maxViewWidthErrorDeg, "deg");
    }

    // Black borders: any of the scenario's edge and corner patches showing no direction code, in
    // every capture that could be read (also those whose view direction couldn't).
    // None in replays, whose real picture covers nearly all of the window.
    void CheckBorders(const Scenario& sc, const Measured& d, Result& r) {
        int decodedCaps = 0, blackCaps = 0;
        for (const auto& c : d.caps) {
            if (!c.decoded) {
                continue;
            }
            decodedCaps++;
            blackCaps += (c.noCode & sc.borderPatches) != 0;
        }

        Check(
            r,
            "frames with black borders",
            decodedCaps ? 100.0 * blackCaps / decodedCaps : 0,
            sc.limits.maxBlackEdgePct,
            "%"
        );
    }

    // Guard: the zoom should stay at its setting (an earlier auto zoom changed it during turns).
    void CheckZoom(const Scenario& sc, const ToolTrace& tools, Result& r) {
        const float rest = *std::min_element(tools.zoom.begin(), tools.zoom.end());
        const float peak = *std::max_element(tools.zoom.begin(), tools.zoom.end());
        int zoomed = 0;
        for (float z : tools.zoom) {
            zoomed += z > rest + 0.02f;
        }
        Check(r, "zoom: peak", peak, -1, "");
        Check(r, "zoom: frames zoomed in", 100.0 * zoomed / tools.zoom.size(), -1, "%");
        Check(
            r,
            "edge limit acted",
            100.0 * tools.edgeClamps / tools.zoom.size(),
            sc.limits.maxEdgeClampPct,
            "% of frames"
        );

        Check(r, "edge correction planned", 100.0 * tools.edgePlanned / tools.zoom.size(), -1, "% of frames");
    }

    Result RunScenario(const Scenario& sc, const fs::path& root) {
        Result r;
        r.name = sc.name;
        if (!sc.error.empty()) {
            r.pass = false;
            r.lines.push_back("  " + sc.error);
            return r;
        }

        const fs::path dir = PrepareFolder(sc, root);
        Measured d;
        d.openxr = UsesOpenXr(sc);
        if (!Record(sc, root, dir, &d.caps, r) || !Measure(sc, dir, &d, r)) {
            return r;
        }

        CheckFrames(sc, d, r);
        if (sc.fake.count("game_start_s")) {
            CheckBeforeGame(sc, d, r);
        }
        CheckSmoothing(sc, d, r);

        // Replays, and synthetic scenarios with the game behind: exactly which pose each game frame was
        // warped from, against the one it was rendered with (Scenario::posesExact).
        if (sc.fake.count("replay") || sc.posesExact) {
            CheckPoses(sc, d, r);
        }
        CheckOnScreen(sc, d, r);
        if (sc.viewOnScreen) {
            CheckViewWidth(sc, d, r);
        }
        if (sc.borderPatches) {
            CheckBorders(sc, d, r);
        }
        if (!d.tools.zoom.empty()) {
            CheckZoom(sc, d.tools, r);
        }

        if (sc.limits.maxTrackErrorDeg >= 0) {
            double delay = 0;
            const double err = TrackErrorDeg(d.m.seen, d.log, &delay);
            Check(r, "view vs head (aligned)", err, sc.limits.maxTrackErrorDeg, "deg");
            Check(r, "  at delay", delay, -1, "ms");
        }

        return r;
    }

    fs::path FindRoot() {
        wchar_t buf[MAX_PATH];
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        fs::path p = fs::path(buf).parent_path();
        while (!p.empty() && !fs::exists(p / "mirror_app" / "simoptix_vr_mirror.exe")) {
            if (p == p.parent_path()) {
                return {};
            }
            p = p.parent_path();
        }

        return p;
    }

    // Reads the command line into the g_ settings and the names of the scenarios to run (none: all).
    // Returns -1 to go on, or the exit code (--list lists the scenarios and ends the run).
    int ParseArguments(
        int argc,
        wchar_t** argv,
        const std::vector<Scenario>& scenarios,
        std::vector<std::string>* wanted
    ) {
        for (int i = 1; i < argc; i++) {
            std::wstring a = argv[i];
            if (a == L"--background") {
                g_foreground = false;
                continue;
            }

            if (a == L"--tool" && i + 1 < argc) {
                g_tool = fs::absolute(argv[++i]);
                continue;
            }

            if ((a == L"--set" || a == L"--fake") && i + 1 < argc) {
                std::wstring kv = argv[++i];
                const size_t eq = kv.find(L'=');
                if (eq != std::wstring::npos) {
                    (a == L"--set" ? g_settings
                                   : g_fake)[Narrow(kv.substr(0, eq))] = Narrow(kv.substr(eq + 1));
                }

                continue;
            }

            if (a == L"--output" && i + 1 < argc) {
                std::wstring v = argv[++i];
                g_outputSize = Narrow(v);
                continue;
            }

            if (a == L"--list") {
                for (const auto& s : scenarios) {
                    printf("%-20s %s\n", s.name.c_str(), s.description.c_str());
                }
                return 0;
            }

            wanted->push_back(Narrow(a));
        }

        return -1;
    }

    // Prints Look-ahead against Instant, for every scenario that ran at both, and the totals.
    // Returns the exit code.
    int
    Summarize(const std::vector<Scenario>& runs, const std::vector<Result>& results, int ran, int failed) {
        const std::vector<Result> compared = CompareSmoothings(runs, results);
        int comparedFailed = 0;
        if (!compared.empty()) {
            printf("\nLook-ahead compared with Instant\n");
        }
        for (const auto& c : compared) {
            printf("\n%s\n", c.name.c_str());
            for (const auto& l : c.lines) {
                printf("%s\n", l.c_str());
            }
            printf("  => %s\n", c.pass ? "PASS" : "FAIL");
            comparedFailed += !c.pass;
        }

        printf(
            "\n%d scenario(s), %d passed, %d failed. Details: vr_mirror\\e2e\\out\\<scenario>\\ (track.csv, "
            "tool.log, fake.log)\n",
            ran,
            ran - failed,
            failed
        );

        if (!compared.empty()) {
            printf("%d comparison(s), %d failed\n", int(compared.size()), comparedFailed);
        }
        return failed || comparedFailed ? 1 : 0;
    }

}

int wmain(int argc, wchar_t** argv) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    fs::path root = FindRoot();
    if (root.empty()) {
        printf("cannot find mirror_app\\simoptix_vr_mirror.exe above the runner; build the tool first\n");
        return 2;
    }

    std::vector<Scenario> scenarios = BuiltInScenarios();
    AddReplayScenarios(root / "e2e" / "replay", &scenarios);

    std::vector<std::string> wanted;
    const int exitCode = ParseArguments(argc, argv, scenarios, &wanted);
    if (exitCode >= 0) {
        return exitCode;
    }

    int failed = 0, ran = 0;
    std::vector<Scenario> runs;
    std::vector<Result> results;

    for (const auto& sc : scenarios) {
        if (!wanted.empty() && std::find(wanted.begin(), wanted.end(), sc.name) == wanted.end()) {
            continue;
        }
        printf("\n%s: %s\n", sc.name.c_str(), sc.description.c_str());
        fflush(stdout);
        Result r = RunScenario(sc, root);
        for (const auto& l : r.lines) {
            printf("%s\n", l.c_str());
        }
        printf("  => %s\n", r.pass ? "PASS" : "FAIL");
        fflush(stdout);
        ran++;
        failed += !r.pass;
        runs.push_back(sc);
        results.push_back(r);
    }

    return Summarize(runs, results, ran, failed);
}
