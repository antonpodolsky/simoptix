// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// local_files_e2e: the end-to-end test of the files SimOptix writes under %LOCALAPPDATA%\simoptix. It runs
// the real, built OBS plugins in a private copy of OBS (local_files_obs.h) and the real OpenXR layer in a
// fake game (fake_layer_game), each with %LOCALAPPDATA% pointed at a test folder that holds older files of
// each kind. Then it checks what each kind left there:
//  - no_game: OBS with the Pimax and OpenXR sources shown, Debug on, and no game (the fake runtime runs but
//    sends no frame). No frame trace is written. Of 12 older crash dumps, the newest 8 are left once the
//    plugins have loaded, which leaves room for the 2 a crash can add.
//  - pimax_game: the Pimax source with the fake runtime's game. One new trace, _ticks and _pimax, with rows.
//    Of it and 12 older traces, the newest 10 are left, and each older one's files (_ticks, and _pimax or
//    _openxr) went together.
//  - openxr_game: the OpenXR source with fake_openxr.exe's game. One new trace. Of it and 10 older ones, the
//    newest 10 are left.
//  - layer: the layer's DLL loaded into "Le Mans Ultimate.exe", with and without the loader's handshake.
//    Each start has its own log, starting with the load line. Of it and 12 older logs, the newest 10 are
//    left. The files the layer wrote before under fixed names stay.
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
    constexpr DWORD kNewSecondMs = 1100;  // apart, so two runs' files never share a second

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
        const char* const marker = "frame trace: ";

        for (size_t at = log.find(marker); at != std::string::npos; at = log.find(marker, at + 1)) {
            const size_t start = at + strlen(marker);
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

    // The new trace: the one stem left that wasn't seeded, with rows in its files.
    void CheckNewTrace(const Case& c, const std::string& log, bool pimax) {
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

        if (pimax) {
            const size_t poses = Lines(c.traces / (made[0] + L"_pimax.csv")).size();
            Check(poses > 50, "its _pimax file has rows (" + std::to_string(poses) + " lines)");
        }
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
        run.sources = {"simoptix_pimax_mirror"};
        run.localAppData = c.appData;
        run.fakePvrScenario = FakePvrScenario(c, 0);
        std::string log;

        if (!RunObsCase(obs, c, run, &log)) {
            return;
        }

        CheckNewTrace(c, log, true);
        CheckTracesKept(c, kOlder);
    }

    void OpenXrGame(const ObsCopy& obs, const fs::path& repo, const fs::path& out) {
        printf("openxr_game\n");
        const Case c = MakeCase(out, "openxr_game");
        SeedTraces(c, kKept);

        // The fake game runs before OBS starts and until it has closed.
        const fs::path scenario = c.dir / "fake_openxr.cfg";
        const std::wstring stopEvent = L"Local\\SimOptix.LocalFilesE2E.Stop."
                                       + std::to_wstring(GetCurrentProcessId());
        const std::string text = "log=" + (c.dir / "fake_openxr.log").string()
                                 + "\nstop_event=" + Narrow(stopEvent) + "\n";
        Touch(scenario, text.c_str());
        FakeGameProcess game;

        if (!LaunchFakeGame(
                c.dir,
                repo / "vr_mirror" / "e2e" / "bin" / "fake_openxr.exe",
                scenario,
                stopEvent,
                &game
            )) {
            Check(false, "the fake game (fake_openxr.exe) started");
            StopFakeGame(game);
            return;
        }

        ObsRun run;
        run.sources = {"simoptix_openxr_mirror"};
        run.localAppData = c.appData;
        run.fakePvrScenario = FakePvrScenario(c, 1e6);
        std::string log;
        const bool ran = RunObsCase(obs, c, run, &log);
        StopFakeGame(game);

        if (!ran) {
            return;
        }

        CheckNewTrace(c, log, false);
        CheckTracesKept(c, kKept);
    }

    // Runs the fake game with the layer in it, with `appData` as its %LOCALAPPDATA%.
    bool RunLayerGame(const fs::path& repo, const fs::path& appData, bool handshake) {
        const fs::path exe = repo / "vr_mirror" / "e2e" / "bin" / "fake_layer_game" / "Le Mans Ultimate.exe";
        const fs::path layer = repo / "vr_mirror" / "frame_sources" / "openxr" / "bin"
                               / "XR_APILAYER_SIMOPTIX_frame_source.dll";
        std::wstring environment = EnvironmentWith({L"LOCALAPPDATA=" + appData.wstring()});
        std::wstring command = L"\"" + exe.wstring() + L"\" \"" + layer.wstring() + L"\""
                               + (handshake ? L" --handshake" : L"");
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

    // The lines of the newest layer log that wasn't seeded.
    std::vector<std::string> NewestLayerLog(const Case& c) {
        const std::vector<std::wstring> logs = TimedFiles(c.simoptix, L"openxr_frame_source", L".log");

        if (logs.empty() || logs.back().compare(0, 28, L"openxr_frame_source_20260101") == 0) {
            return {};
        }

        return Lines(c.simoptix / logs.back());
    }

    bool HasLine(const std::vector<std::string>& lines, const char* text) {
        for (const std::string& line : lines) {
            if (line.find(text) != std::string::npos) {
                return true;
            }
        }

        return false;
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

        Check(RunLayerGame(repo, c.appData, true), "the layer loaded and accepted the handshake");
        std::vector<std::string> lines = NewestLayerLog(c);
        Check(
            !lines.empty() && lines[0].find("loaded into process") != std::string::npos,
            "a new log, starting with the load line"
        );
        Check(HasLine(lines, "loader offers layer interface"), "...then the handshake");
        size_t logs = TimedFiles(c.simoptix, L"openxr_frame_source", L".log").size();
        Check(logs == kKept, std::to_string(kKept) + " logs left (" + std::to_string(logs) + ")");
        Check(
            !Exists(c.simoptix / (SeedStem(L"openxr_frame_source", kOlder - kKept) + L".log"))
                && Exists(c.simoptix / (SeedStem(L"openxr_frame_source", kOlder - kKept + 1) + L".log")),
            "the oldest are the ones gone"
        );
        CheckOthersStay(c.simoptix, fixedNames);

        Sleep(kNewSecondMs);
        Check(RunLayerGame(repo, c.appData, false), "the layer loaded (no handshake)");
        lines = NewestLayerLog(c);
        Check(
            lines.size() == 1 && lines[0].find("loaded into process") != std::string::npos,
            "a new log with the load line alone"
        );

        Sleep(kNewSecondMs);
        Check(RunLayerGame(repo, c.appData, true), "the layer loaded and accepted the handshake again");
        logs = TimedFiles(c.simoptix, L"openxr_frame_source", L".log").size();
        Check(
            logs == kKept,
            "the next start with a handshake leaves " + std::to_string(kKept) + " logs ("
                + std::to_string(logs) + ")"
        );
    }

}

int wmain() {
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
    OpenXrGame(obs, repo, out);
    Layer(repo, out);

    printf(
        "%s (%d failed). The runs' files: %ls\n",
        g_failures ? "FAILED" : "passed",
        g_failures,
        out.c_str()
    );
    return g_failures ? 1 : 0;
}
