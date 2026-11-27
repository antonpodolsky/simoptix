// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// local_files_e2e: the end-to-end test of the files SimOptix writes under %LOCALAPPDATA%\simoptix. It runs
// the real, built OBS plugins in a private copy of OBS (local_files_obs.h) and the real OpenXR layer in a
// fake game (fake_layer_game), each with %LOCALAPPDATA% pointed at a test folder that holds older files of
// each kind. Then it checks what each kind left there:
//  - no_game: OBS with the Pimax and OpenXR sources shown, Debug on, and no game (the fake runtime runs but
//    sends no frame). No frame trace is written. OBS's log has each source's settings, and no eye. Of 12
//    older crash dumps, the newest 8 are left once the plugins have loaded, which leaves room for the 2 a
//    crash can add.
//  - pimax_game: the Pimax source with the fake runtime's game, and a G-meter, recorded by OBS from its
//    start. OBS's log has the settings and the game's eye. One new trace, _ticks and _pimax, with rows, its
//    column header first and then the settings and the eye. Of it and 12 older traces, the newest 10 are
//    left, and each older one's files (_ticks, and _pimax or _openxr) went together. Beside the recording, a
//    notes file has the version, the settings, the eye and the frame trace. It runs past 10 s: the 10 s
//    stats are in the trace and not in OBS's log. OBS's log has the G-meter's lines, and the telemetry's
//    from the reader's first pass.
//  - no_debug: the Pimax source with the fake runtime's game, and a G-meter, with Debug off, recorded by
//    OBS. No notes file beside the recording, no frame trace, no VR Mirror or Overlays line in OBS's log but
//    the versions, and no telemetry line.
//  - openxr_game, openxr_no_debug, openxr_overlay_debug, openxr_mirror_debug: the OpenXR source and a
//    performance overlay with fake_openxr.exe's game, which runs the layer's own log. Both with Debug on,
//    both off, only the overlay's on, only VR Mirror's on. The fake game's log shows that both read.
//    Each plugin logs as its own Debug says, and nothing but its version with it off. With VR Mirror's on,
//    the settings and the eye are logged, and one new trace, _ticks and _openxr, has rows, as in
//    pimax_game: of it and 10 older traces, the newest 10 are left. With it off, there is no trace. The
//    layer writes one log, with the line of each reader that has Debug on, and none with both off.
//  - layer: the layer's DLL loaded into "Le Mans Ultimate.exe", with and without the loader's handshake.
//    No VR Mirror reads its frames, so none says it has Debug on, and neither start leaves a log. Then a
//    handshake for another layer, which the layer refuses: that failure is written whatever Debug says, in
//    a new log that has none of the held lines. Of it and 12 older logs, the newest 10 are left. The files
//    the layer wrote before under fixed names stay.
// Each run checks that names of any other form stay. OBS and LMU must be closed. Takes about a minute (the
// first run longer: it copies OBS).
//
// Usage: local_files_e2e.exe, from anywhere. It works in vr_mirror\e2e\out\local_files\.
#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "local_files_folder.h"
#include "local_files_obs.h"
#include "shared/unique_handle.h"
#include "tool_process.h"

namespace {

    constexpr int kKept = 10;  // of each kind, as shared\local_files.cpp keeps
    constexpr int kOlder = 12;
    constexpr DWORD kNewSecondMs = 1100;                   // apart, so two runs' files never share a second
    constexpr const char* kTraceLogged = "frame trace: ";  // OBS's log, as a trace starts

    int g_failures = 0;

    void Check(bool ok, const std::string& what) {
        printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
        g_failures += ok ? 0 : 1;
    }

    bool IsRunning(const wchar_t* exe) {
        const UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        PROCESSENTRY32W entry = {sizeof(entry)};

        if (!snapshot || !Process32FirstW(snapshot.Get(), &entry)) {
            return false;
        }

        do {
            if (_wcsicmp(entry.szExeFile, exe) == 0) {
                return true;
            }
        } while (Process32NextW(snapshot.Get(), &entry));

        return false;
    }

    std::string Narrow(const std::wstring& text) {
        return fs::path(text).string();
    }

    // The trace each "frame trace: <stem>_ticks.csv" line of OBS's log names, as a file name stem.
    std::vector<std::wstring> TracesLogged(const std::string& log) {
        std::vector<std::wstring> stems;

        for (size_t at = log.find(kTraceLogged); at != std::string::npos;
             at = log.find(kTraceLogged, at + 1)) {
            const size_t start = at + strlen(kTraceLogged);
            const size_t end = log.find("_ticks.csv", start);

            if (end != std::string::npos) {
                stems.push_back(fs::path(log.substr(start, end - start)).filename().wstring());
            }
        }

        return stems;
    }

    bool Contains(const std::vector<std::wstring>& list, const std::wstring& item) {
        for (const std::wstring& i : list) {
            if (i == item) {
                return true;
            }
        }

        return false;
    }

    struct Case {
        fs::path dir;
        fs::path appData;  // the test %LOCALAPPDATA%
        fs::path simoptix;
        fs::path traces;
    };

    Case MakeCase(const fs::path& out, const char* name) {
        Case c;
        c.dir = out / name;
        c.appData = c.dir / "appdata";
        c.simoptix = c.appData / "simoptix";
        c.traces = c.simoptix / "traces";
        std::error_code ec;
        fs::create_directories(c.traces, ec);
        return c;
    }

    // The fake runtime's settings, with its game starting after `gameStartS` (1e6: never).
    fs::path FakePvrScenario(const Case& c, double gameStartS) {
        const fs::path file = c.dir / "fake_pvr.cfg";
        const std::string text = "log=" + (c.dir / "fake_pvr.log").string()
                                 + "\ngame_start_s=" + std::to_string(gameStartS) + "\n";
        Touch(file, text.c_str());
        return file;
    }

    // The frame source's own file of older trace `i`: _pimax for the even ones, _openxr for the odd ones.
    const wchar_t* SourceEnding(int i) {
        return i % 2 == 0 ? L"_pimax.csv" : L"_openxr.csv";
    }

    // Older traces 0 to count-1, each with a _ticks file and its frame source's own file, and files of other
    // names beside them.
    void SeedTraces(const Case& c, int count) {
        for (int i = 0; i < count; i++) {
            Touch(c.traces / (SeedStem(L"trace", i) + L"_ticks.csv"));
            Touch(c.traces / (SeedStem(L"trace", i) + SourceEnding(i)));
        }

        Touch(c.traces / L"trace_notes.txt");
        Touch(c.traces / L"mine.csv");
    }

    void CheckOthersStay(const fs::path& dir, const std::vector<const wchar_t*>& names) {
        bool stay = true;

        for (const wchar_t* name : names) {
            stay = stay && Exists(dir / name);
        }

        Check(stay, "files of other names stay");
    }

    bool StartsWith(const std::string& text, const char* start) {
        return text.compare(0, strlen(start), start) == 0;
    }

    bool HasLineStarting(const std::vector<std::string>& lines, const char* start) {
        for (const std::string& line : lines) {
            if (StartsWith(line, start)) {
                return true;
            }
        }

        return false;
    }

    // What VR Mirror logs about what a source (its frame source's `name`) runs with: the settings as the
    // scene has them (Instant smoothing), and the game's eye once its frames came (`withEye`).
    void CheckSetupLogged(const std::string& log, const std::string& name, bool withEye) {
        const bool settings = log.find("settings (" + name + "): smoothing=1 ") != std::string::npos;
        Check(settings, "OBS's log has the settings (" + name + ")");

        const bool eye = log.find("eye (" + name + "): left ") != std::string::npos;
        Check(eye == withEye, withEye ? "...and the game's eye" : "...and no eye, while no game runs");
    }

    // The new trace's _ticks file: the column header first, then the settings and the eye.
    void CheckTraceSetup(const fs::path& ticks) {
        const std::vector<std::string> lines = Lines(ticks);
        Check(!lines.empty() && StartsWith(lines[0], "stamp,"), "its first line is the column header");
        Check(HasLineStarting(lines, "#settings,smoothing=1 "), "it has the settings");
        Check(HasLineStarting(lines, "#eye,left "), "it has the game's eye");
    }

    bool HasLine(const std::vector<std::string>& lines, const char* text) {
        for (const std::string& line : lines) {
            if (line.find(text) != std::string::npos) {
                return true;
            }
        }

        return false;
    }

    // A run past 10 s with Debug on: the 10 s stats are in the trace, as `#stats,` lines, and not in OBS's
    // log.
    void CheckStatsInTrace(const Case& c, const std::string& log) {
        const std::vector<std::wstring> stems = StemsOf(TimedFiles(c.traces, L"trace", L".csv"), L"trace");
        std::vector<std::string> lines;

        for (const std::wstring& stem : stems) {
            if (stem.compare(0, 14, L"trace_20260101") != 0) {
                lines = Lines(c.traces / (stem + L"_ticks.csv"));
            }
        }

        Check(
            HasLine(lines, "#stats,") && HasLine(lines, " ticks in 10 s: "),
            "the trace has the 10 s stats"
        );
        Check(HasLine(lines, "#stats,footprint: "), "...and the footprint");
        Check(log.find("ticks in 10 s") == std::string::npos, "OBS's log has no 10 s stats");
        Check(log.find("footprint: Pimax") == std::string::npos, "...and no footprint");
    }

    // With Debug off, OBS's log has the plugin's version line and no other line of its (`tag`, as
    // "[simoptix_vr_mirror] "): every other one is Debug's (the failures excepted, and this run has none).
    // The telemetry's lines carry the tag of the plugin that runs its reader.
    void CheckOnlyVersionLogged(const std::string& log, const std::string& tag, const std::string& plugin) {
        std::string others;
        size_t count = 0;

        for (size_t at = log.find(tag); at != std::string::npos; at = log.find(tag, at + 1)) {
            const size_t end = log.find('\n', at);
            const std::string line = log.substr(
                at + tag.size(),
                end == std::string::npos ? end : end - at - tag.size()
            );

            if (!StartsWith(line, "loaded, version ")) {
                others += (count++ ? "; " : "") + line.substr(0, 60);
            }
        }

        Check(
            count == 0,
            "OBS's log has no " + plugin + " line but the version, with Debug off"
                + (count ? " (" + others + ")" : "")
        );
    }

    // A G-meter shown with Debug on: OBS's log has its lines, and the telemetry's it holds, from the reader's
    // first pass (no game runs, so it waits for one).
    void CheckMeterLogged(const std::string& log) {
        Check(
            log.find("[simoptix_overlays] G-Meter Overlay: drawn at ") != std::string::npos,
            "OBS's log has the G-meter's lines, with Debug on"
        );
        Check(log.find("telemetry: reader started") != std::string::npos, "...and the telemetry's");

        const bool firstPass = log.find("telemetry: waiting for a game") != std::string::npos
                               || log.find("telemetry: built without any game") != std::string::npos
                               || log.find("telemetry: connected to ") != std::string::npos;
        Check(firstPass, "...from the reader's first pass");
    }

    // The recording in `dir`, and the notes file beside it with the version, and the settings, the eye and
    // the frame trace of the source (its frame source's `name`).
    void CheckRecordingNotes(const fs::path& dir, const std::string& name) {
        std::vector<fs::path> recordings;
        std::error_code ec;

        for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
            if (entry.path().extension() == ".mkv") {
                recordings.push_back(entry.path());
            }
        }

        Check(recordings.size() == 1, "one recording (" + std::to_string(recordings.size()) + ")");

        if (recordings.size() != 1) {
            return;
        }

        fs::path notesFile = recordings[0];
        notesFile.replace_extension(".simoptix.txt");
        const std::vector<std::string> notes = Lines(notesFile);
        Check(!notes.empty(), "a notes file beside it, " + notesFile.filename().string());
        Check(!notes.empty() && StartsWith(notes[0], "SimOptix VR Mirror "), "...naming the version first");

        bool settings = false, eye = false, trace = false;

        for (const std::string& line : notes) {
            settings = settings || line.find(name + ": settings: smoothing=1 ") != std::string::npos;
            eye = eye || line.find(name + ": eye: left ") != std::string::npos;
            trace = trace || line.find(name + ": frame trace: ") != std::string::npos;
        }

        Check(settings, "...with the settings");
        Check(eye, "...with the game's eye");
        Check(trace, "...with the frame trace");
    }

    // The new trace: the one stem left that wasn't seeded, with rows in its _ticks file and in its frame
    // source's own file (`sourceEnding`: "_pimax.csv" or "_openxr.csv").
    void CheckNewTrace(const Case& c, const std::string& log, const wchar_t* sourceEnding) {
        const std::vector<std::wstring> stems = StemsOf(TimedFiles(c.traces, L"trace", L".csv"), L"trace");
        std::vector<std::wstring> made;

        for (const std::wstring& stem : stems) {
            if (stem.compare(0, 14, L"trace_20260101") != 0) {
                made.push_back(stem);
            }
        }

        Check(made.size() == 1, "one new trace (" + std::to_string(made.size()) + ")");

        if (made.size() != 1) {
            return;
        }

        const std::vector<std::wstring> logged = TracesLogged(log);
        Check(logged.size() == 1 && logged[0] == made[0], "OBS's log names it");

        const size_t ticks = Lines(c.traces / (made[0] + L"_ticks.csv")).size();
        Check(ticks > 50, "its _ticks file has rows (" + std::to_string(ticks) + " lines)");
        CheckTraceSetup(c.traces / (made[0] + L"_ticks.csv"));

        const size_t own = Lines(c.traces / (made[0] + sourceEnding)).size();
        const std::string ending = Narrow(sourceEnding);
        const std::string label = ending.substr(0, ending.size() - 4);  // without ".csv"
        Check(own > 50, "its " + label + " file has rows (" + std::to_string(own) + " lines)");
    }

    // After a run that made one trace, from `seeded` older ones: the newest 10 are left.
    void CheckTracesKept(const Case& c, int seeded) {
        const std::vector<std::wstring> files = TimedFiles(c.traces, L"trace", L".csv");
        const std::vector<std::wstring> stems = StemsOf(files, L"trace");
        Check(
            stems.size() == kKept,
            std::to_string(kKept) + " traces left (" + std::to_string(stems.size()) + ")"
        );

        const int firstKept = seeded + 1 - kKept;
        bool oldestGone = true, newestStay = true;

        for (int i = 0; i < seeded; i++) {
            const std::wstring stem = SeedStem(L"trace", i);
            const bool ticks = Exists(c.traces / (stem + L"_ticks.csv"));
            const bool own = Exists(c.traces / (stem + SourceEnding(i)));

            if (i < firstKept && (ticks || own)) {
                oldestGone = false;
            }

            if (i >= firstKept && (!ticks || !own)) {
                newestStay = false;
            }
        }

        Check(oldestGone, "the oldest traces are gone, their _pimax and _openxr files with them");
        Check(newestStay, "the newer older traces stay, with their _pimax and _openxr files");
        CheckOthersStay(c.traces, {L"trace_notes.txt", L"mine.csv"});
    }

    bool RunObsCase(const ObsCopy& obs, const Case& c, const ObsRun& run, std::string* log) {
        std::string error;
        const bool ran = RunObs(obs, run, log, &error);
        Touch(c.dir / "obs.log", log->c_str());
        Check(ran, ran ? "OBS ran and closed" : "OBS ran: " + error);
        Check(log->find("[simoptix_vr_mirror] loaded") != std::string::npos, "VR Mirror loaded");
        Check(log->find("[simoptix_overlays] loaded") != std::string::npos, "Overlays loaded");
        return ran;
    }

    void NoGame(const ObsCopy& obs, const fs::path& out) {
        printf("no_game\n");
        const Case c = MakeCase(out, "no_game");

        for (int i = 0; i < kOlder; i++) {
            Touch(
                c.simoptix
                / (SeedStem(L"obs", i) + (i % 2 ? L"_c0000005_vr_mirror.dmp" : L"_c0000005_overlays.dmp"))
            );
        }

        Touch(c.simoptix / L"obs_crash.dmp");

        ObsRun run;
        run.sources = {"simoptix_pimax_mirror", "simoptix_openxr_mirror"};
        run.localAppData = c.appData;
        run.fakePvrScenario = FakePvrScenario(c, 1e6);
        std::string log;
        RunObsCase(obs, c, run, &log);

        Check(TimedFiles(c.traces, L"trace", L".csv").empty(), "no frame trace while no game runs");
        Check(TracesLogged(log).empty(), "OBS's log names no frame trace");
        CheckSetupLogged(log, "Pimax session", false);
        CheckSetupLogged(log, "OpenXR layer", false);

        const std::vector<std::wstring> dumps = TimedFiles(c.simoptix, L"obs", L".dmp");
        Check(
            dumps.size() == kKept - 2,
            "the newest " + std::to_string(kKept - 2) + " crash dumps left (" + std::to_string(dumps.size())
                + ")"
        );
        Check(
            !dumps.empty() && dumps.front().compare(0, 19, SeedStem(L"obs", kOlder - kKept + 2)) == 0,
            "the oldest left is the right one"
        );
        CheckOthersStay(c.simoptix, {L"obs_crash.dmp"});
    }

    void PimaxGame(const ObsCopy& obs, const fs::path& out) {
        printf("pimax_game\n");
        const Case c = MakeCase(out, "pimax_game");
        SeedTraces(c, kOlder);

        ObsRun run;
        run.sources = {"simoptix_pimax_mirror", "simoptix_gmeter_overlay"};
        run.localAppData = c.appData;
        run.fakePvrScenario = FakePvrScenario(c, 0);
        run.holdUntilLogged = kTraceLogged;
        run.holdMs = 14000;  // past the first 10 s of stats
        run.recordTo = c.dir / "recordings";
        std::error_code ec;
        fs::remove_all(run.recordTo, ec);
        fs::create_directories(run.recordTo, ec);
        std::string log;

        if (!RunObsCase(obs, c, run, &log)) {
            return;
        }

        CheckSetupLogged(log, "Pimax session", true);
        CheckStatsInTrace(c, log);
        CheckNewTrace(c, log, L"_pimax.csv");
        CheckTracesKept(c, kOlder);
        CheckRecordingNotes(run.recordTo, "Pimax session");
        CheckMeterLogged(log);
    }

    // The Pimax source and a G-meter with the fake runtime's game and Debug off, recorded by OBS: the
    // recording gets no notes file, no frame trace is written, and OBS's log has no settings or eye, and no
    // line of either plugin or the telemetry but the versions, since all are Debug's.
    void NoDebug(const ObsCopy& obs, const fs::path& out) {
        printf("no_debug\n");
        const Case c = MakeCase(out, "no_debug");

        ObsRun run;
        run.sources = {"simoptix_pimax_mirror", "simoptix_gmeter_overlay"};
        run.debug = false;
        run.holdMs = 12000;  // from OBS's window: the source starts, and runs past 10 s of stats
        run.localAppData = c.appData;
        run.fakePvrScenario = FakePvrScenario(c, 0);
        run.recordTo = c.dir / "recordings";
        std::error_code ec;
        fs::remove_all(run.recordTo, ec);
        fs::create_directories(run.recordTo, ec);
        std::string log;

        if (!RunObsCase(obs, c, run, &log)) {
            return;
        }

        std::vector<fs::path> recordings, notes;

        for (const fs::directory_entry& entry : fs::directory_iterator(run.recordTo, ec)) {
            const std::string name = entry.path().filename().string();
            const bool isNotes = name.size() > 13 && name.compare(name.size() - 13, 13, ".simoptix.txt") == 0;
            (isNotes ? notes : recordings).push_back(entry.path());
        }

        Check(recordings.size() == 1, "one recording (" + std::to_string(recordings.size()) + ")");
        Check(notes.empty(), "no notes file beside it, with Debug off");
        Check(TimedFiles(c.traces, L"trace", L".csv").empty(), "no frame trace, with Debug off");
        Check(log.find("settings (") == std::string::npos, "no settings in OBS's log, with Debug off");
        Check(log.find("eye (") == std::string::npos, "no eye in OBS's log, with Debug off");
        CheckOnlyVersionLogged(log, "[simoptix_vr_mirror] ", "VR Mirror");
        CheckOnlyVersionLogged(log, "[simoptix_overlays] ", "Overlays");
        Check(log.find("telemetry: ") == std::string::npos, "no telemetry line, with Debug off");
    }

    // The lines of the newest layer log that wasn't seeded.
    std::vector<std::string> NewestLayerLog(const Case& c) {
        const std::vector<std::wstring> logs = TimedFiles(c.simoptix, L"openxr_frame_source", L".log");

        if (logs.empty() || logs.back().compare(0, 28, L"openxr_frame_source_20260101") == 0) {
            return {};
        }

        return Lines(c.simoptix / logs.back());
    }

    // The fake OpenXR game, with the layer's own log in this case's %LOCALAPPDATA%, and OBS with `run`'s
    // sources meanwhile. The fake game runs before OBS starts and until it has closed. False if OBS didn't
    // run (the reason is checked).
    bool
    RunOpenXrCase(const ObsCopy& obs, const fs::path& repo, const Case& c, ObsRun run, std::string* log) {
        const fs::path scenario = c.dir / "fake_openxr.cfg";
        const std::wstring stopEvent = L"Local\\SimOptix.LocalFilesE2E.Stop."
                                       + std::to_wstring(GetCurrentProcessId()) + L"."
                                       + c.dir.filename().wstring();
        // A quarter of the default image's sides: the test checks files, not speed, and on a machine
        // with no graphics card the full-size image left the layer's copies pending (at about 4 fps).
        const std::string text = "log=" + (c.dir / "fake_openxr.log").string() + "\nstop_event="
                                 + Narrow(stopEvent) + "\nwidth=1271\nheight=1042\nlayer_log=1\n";
        Touch(scenario, text.c_str());
        FakeGameProcess game;

        if (!LaunchFakeGame(
                c.dir,
                repo / "vr_mirror" / "e2e" / "bin" / "fake_openxr.exe",
                scenario,
                stopEvent,
                &game,
                {L"LOCALAPPDATA=" + c.appData.wstring()}
            )) {
            Check(false, "the fake game (fake_openxr.exe) started");
            StopFakeGame(game);
            return false;
        }

        run.localAppData = c.appData;
        run.fakePvrScenario = FakePvrScenario(c, 1e6);
        const bool ran = RunObsCase(obs, c, run, log);
        StopFakeGame(game);
        return ran;
    }

    // From the fake OpenXR game's log: VR Mirror copied the layer's frames, and the performance overlay had
    // the layer time them, whatever either logged.
    void CheckSourcesRead(const Case& c) {
        size_t copied = 0, timed = 0;

        for (const std::string& line : Lines(c.dir / "fake_openxr.log")) {
            unsigned frames = 0, n = 0;

            if (sscanf_s(line.c_str(), "# frame source: %u frames, %u copied", &frames, &n) == 2) {
                copied += n;
            }

            if (sscanf_s(line.c_str(), "# frame timing: %u frames timed", &n) == 1) {
                timed += n;
            }
        }

        Check(copied > 0, "VR Mirror read the layer's frames (" + std::to_string(copied) + " copied)");
        Check(timed > 0, "the performance overlay read their times (" + std::to_string(timed) + " timed)");
    }

    // The layer's log while VR Mirror (Debug `mirrorDebug`) and the performance overlay (`overlayDebug`) read
    // the layer: none while both have Debug off, else one, with the line of each reader that has Debug on,
    // logged as it came. The layer drops its lines while only readers with Debug off are there, and which
    // reader comes first isn't fixed, so the load line is sure to be first only while both have Debug on.
    void CheckLayerLog(const Case& c, bool mirrorDebug, bool overlayDebug) {
        const size_t logs = TimedFiles(c.simoptix, L"openxr_frame_source", L".log").size();

        if (!mirrorDebug && !overlayDebug) {
            Check(logs == 0, "no layer log, with both readers' Debug off (" + std::to_string(logs) + ")");
            return;
        }

        const std::vector<std::string> lines = NewestLayerLog(c);
        Check(logs == 1 && !lines.empty(), "one layer log (" + std::to_string(logs) + ")");

        if (mirrorDebug && overlayDebug) {
            Check(
                !lines.empty() && lines[0].find("loaded into process") != std::string::npos,
                "...starting with the load line"
            );
        }

        if (mirrorDebug) {
            Check(
                HasLine(lines, "frame source: VR Mirror reads the frames"),
                "...with VR Mirror reading the frames"
            );
        }

        if (overlayDebug) {
            Check(
                HasLine(lines, "frame timing: SimOptix reads the game's frame times"),
                "...with the performance overlay reading their times"
            );
        }
    }

    // The performance overlay's lines in OBS's log with Debug on (`debug`), and none but the version with it
    // off.
    void CheckPerformanceLogged(const std::string& log, bool debug) {
        if (!debug) {
            CheckOnlyVersionLogged(log, "[simoptix_overlays] ", "Overlays");
            return;
        }

        Check(
            log.find("[simoptix_overlays] Performance Overlay: drawn at ") != std::string::npos
                && log.find("[simoptix_overlays] performance: reading the game's frame times")
                       != std::string::npos,
            "OBS's log has the performance overlay's lines, with Debug on"
        );
    }

    // The OpenXR source and a performance overlay with the fake OpenXR game, VR Mirror's Debug
    // `mirrorDebug` and the overlay's `overlayDebug`. Each plugin logs as its own Debug says. The layer's
    // log is written while either has Debug on. With VR Mirror's on, the settings and the eye are logged,
    // and one new trace, _ticks and _openxr, has rows, as in pimax_game: of it and 10 older traces, the
    // newest 10 are left. With it off, there is no trace.
    void OpenXrCase(
        const ObsCopy& obs,
        const fs::path& repo,
        const fs::path& out,
        const char* name,
        bool mirrorDebug,
        bool overlayDebug
    ) {
        printf("%s\n", name);
        const Case c = MakeCase(out, name);

        if (mirrorDebug) {
            SeedTraces(c, kKept);
        }

        ObsRun run;
        run.sources = {"simoptix_openxr_mirror", "simoptix_performance_overlay"};
        run.debug = mirrorDebug || overlayDebug;

        if (!mirrorDebug) {
            run.debugOff.push_back("simoptix_openxr_mirror");
        }

        if (!overlayDebug) {
            run.debugOff.push_back("simoptix_performance_overlay");
        }

        if (mirrorDebug) {
            run.holdUntilLogged = kTraceLogged;
        } else {
            run.holdMs = 8000;  // from OBS's window: both sources start and read
        }

        std::string log;

        if (!RunOpenXrCase(obs, repo, c, run, &log)) {
            return;
        }

        if (mirrorDebug) {
            CheckSetupLogged(log, "OpenXR layer", true);
            CheckNewTrace(c, log, L"_openxr.csv");
            CheckTracesKept(c, kKept);
        } else {
            Check(TimedFiles(c.traces, L"trace", L".csv").empty(), "no frame trace, with Debug off");
            CheckOnlyVersionLogged(log, "[simoptix_vr_mirror] ", "VR Mirror");
        }

        CheckPerformanceLogged(log, overlayDebug);
        CheckSourcesRead(c);
        CheckLayerLog(c, mirrorDebug, overlayDebug);
    }

    // Runs the fake game with the layer in it, with `appData` as its %LOCALAPPDATA% and the game's `option`
    // (L"--handshake", L"--wrong-name" or L"" for none).
    bool RunLayerGame(const fs::path& repo, const fs::path& appData, const wchar_t* option) {
        const fs::path exe = repo / "vr_mirror" / "e2e" / "bin" / "fake_layer_game" / "Le Mans Ultimate.exe";
        const fs::path layer = repo / "vr_mirror" / "frame_sources" / "openxr" / "bin"
                               / "XR_APILAYER_SIMOPTIX_frame_source.dll";
        std::wstring environment = EnvironmentWith({L"LOCALAPPDATA=" + appData.wstring()});
        std::wstring command = L"\"" + exe.wstring() + L"\" \"" + layer.wstring() + L"\" " + option;
        STARTUPINFOW startup = {sizeof(startup)};
        PROCESS_INFORMATION process = {};

        const BOOL started = CreateProcessW(
            exe.c_str(),
            command.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_UNICODE_ENVIRONMENT,
            environment.data(),
            exe.parent_path().c_str(),
            &startup,
            &process
        );

        if (!started) {
            return false;
        }

        const UniqueHandle processHandle(process.hProcess);
        const UniqueHandle threadHandle(process.hThread);
        DWORD code = 1;

        if (WaitForSingleObject(processHandle.Get(), 10000) != WAIT_OBJECT_0) {
            TerminateProcess(processHandle.Get(), 1);
        } else {
            GetExitCodeProcess(processHandle.Get(), &code);
        }

        return code == 0;
    }

    void Layer(const fs::path& repo, const fs::path& out) {
        printf("layer\n");
        const Case c = MakeCase(out, "layer");
        const std::vector<const wchar_t*> fixedNames = {
            L"openxr_frame_source.log",
            L"openxr_frame_source.prev.log",
            L"openxr_frame_source_loads.log",
        };

        for (int i = 0; i < kOlder; i++) {
            Touch(c.simoptix / (SeedStem(L"openxr_frame_source", i) + L".log"));
        }

        for (const wchar_t* name : fixedNames) {
            Touch(c.simoptix / name);
        }

        // No VR Mirror reads the frames, so none says it has Debug on: the layer's lines stay in memory.
        Check(RunLayerGame(repo, c.appData, L"--handshake"), "the layer loaded and accepted the handshake");
        size_t logs = TimedFiles(c.simoptix, L"openxr_frame_source", L".log").size();
        Check(
            logs == kOlder,
            "no new log and none deleted, with no VR Mirror's Debug (" + std::to_string(logs) + " logs)"
        );

        Sleep(kNewSecondMs);
        Check(RunLayerGame(repo, c.appData, L""), "the layer loaded (no handshake)");
        logs = TimedFiles(c.simoptix, L"openxr_frame_source", L".log").size();
        Check(logs == kOlder, "...nor with no handshake (" + std::to_string(logs) + " logs)");

        // A failure is written whatever Debug says, and the log is made for it.
        Sleep(kNewSecondMs);
        Check(
            RunLayerGame(repo, c.appData, L"--wrong-name"),
            "the layer refused a handshake for another layer"
        );
        const std::vector<std::string> lines = NewestLayerLog(c);
        Check(
            !lines.empty()
                && lines[0].find("handshake refused: the layer name doesn't match") != std::string::npos,
            "a new log, starting with the refusal"
        );
        Check(!HasLine(lines, "loaded into process"), "...and without the lines Debug holds back");
        logs = TimedFiles(c.simoptix, L"openxr_frame_source", L".log").size();
        Check(logs == kKept, std::to_string(kKept) + " logs left (" + std::to_string(logs) + ")");
        Check(
            !Exists(c.simoptix / (SeedStem(L"openxr_frame_source", kOlder - kKept) + L".log"))
                && Exists(c.simoptix / (SeedStem(L"openxr_frame_source", kOlder - kKept + 1) + L".log")),
            "the oldest are the ones gone"
        );
        CheckOthersStay(c.simoptix, fixedNames);
    }

}

int wmain() {
    if (!EndChildrenWithThisProcess()) {
        printf("warning: a run stopped midway may leave OBS or the fake game running\n");
    }

    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    const fs::path repo = fs::path(self).parent_path().parent_path().parent_path().parent_path();
    const fs::path out = repo / "vr_mirror" / "e2e" / "out" / "local_files";

    if (IsRunning(L"obs64.exe") || IsRunning(L"Le Mans Ultimate.exe")) {
        printf("close OBS and LMU first\n");
        return 2;
    }

    std::error_code ec;
    fs::remove_all(out, ec);
    fs::create_directories(out, ec);

    ObsCopy obs;
    std::string error;

    if (!PrepareObs(repo, out, &obs, &error)) {
        printf("%s\n", error.c_str());
        return 2;
    }

    NoGame(obs, out);
    PimaxGame(obs, out);
    NoDebug(obs, out);
    OpenXrCase(obs, repo, out, "openxr_game", true, true);
    OpenXrCase(obs, repo, out, "openxr_no_debug", false, false);
    OpenXrCase(obs, repo, out, "openxr_overlay_debug", false, true);
    OpenXrCase(obs, repo, out, "openxr_mirror_debug", true, false);
    Layer(repo, out);

    printf(
        "%s (%d failed). The runs' files: %ls\n",
        g_failures ? "FAILED" : "passed",
        g_failures,
        out.c_str()
    );
    return g_failures ? 1 : 0;
}
