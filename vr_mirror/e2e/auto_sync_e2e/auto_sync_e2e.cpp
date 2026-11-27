// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// auto_sync_e2e: VR Mirror's auto sync end to end, in the real, built OBS plugin, on a real recording of
// LMU's menus: the game's sound put in step with the view by itself. The fake game (fake_openxr.exe) plays a
// cut of the recording (vr_mirror\recording_analysis\export_menu_cut.py): each frame from the camera it was
// recorded from, and its sound through Windows' default output device, a chosen lag late. Auto sync
// measures the game's sound at the hovers.
// OBS (local_files_obs.h) shows the OpenXR source and captures Desktop Audio. The sound in a real recording
// has a lag of its own after its image, which isn't known exactly, so two runs, with different lags, check
// that the game's sound moves by as much as the lags. In the second, once auto sync is done, the game's
// frames stop for a few seconds, as when LMU restarts, and the menus play again: auto sync measures again,
// and settles where it did. In the first, Desktop Audio has a sync offset of its own. In both, the offset OBS
// saves at the end is the view's delay and auto sync's correction alone, with the own offset kept under it.
//
// The cut shows LMU's menus, with cars and tracks, so it stays in the data folder: in the folder
// AUTO_SYNC_MENU_CUT names, else SIMOPTIX_DATA\auto_sync_menu_cut. Needs OBS installed and a sound output
// device, OBS and LMU closed, and no other sound playing. The menus' sound is heard. Takes about half a
// minute.
//
// Usage: auto_sync_e2e.exe, from anywhere. It works in vr_mirror\e2e\out\auto_sync\.
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "local_files_folder.h"
#include "local_files_obs.h"
#include "shared/unique_handle.h"
#include "tool_process.h"

namespace {

    // The plan, in seconds from its start: the menus for up to kMenuMostS. It starts once auto sync says it
    // is measuring, so no hover goes by before. In the run that checks measuring again, the game's frames
    // stop for kRestartS once auto sync is done, as when the game restarts, and the menus play once more.
    constexpr double kMenuMostS = 35, kRestartS = 3;

    // At most this long from the sources' start to auto sync measuring.
    constexpr double kMeasuringMostS = 15;

    constexpr const char* kMeasuring = "[simoptix_vr_mirror] auto sync: measuring, ";

    // The two runs' lags against the lags put in, and the second measuring's against the first: only auto
    // sync's measuring is left in them, which times each highlight to the frame.
    constexpr double kMovedToleranceS = 0.01;

    constexpr const char* kDone = "[simoptix_vr_mirror] auto sync done, ";
    constexpr const char* kShifted = "[simoptix_vr_mirror] audio sync: ";
    constexpr const char*
        kShiftKey = "simoptix_vr_mirror_sync_shift_ns";  // the plugin's, in private settings
    constexpr const char* kGameSound = "Desktop Audio";
    constexpr double kSavedToleranceS = 0.0015;  // the delay and the correction are logged to the millisecond

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

    std::vector<std::string> Split(const std::string& line) {
        std::vector<std::string> parts;
        std::stringstream stream(line);
        std::string part;

        while (std::getline(stream, part, ',')) {
            parts.push_back(part);
        }

        return parts;
    }

    // The menu cut's folder: from AUTO_SYNC_MENU_CUT, else SIMOPTIX_DATA's; empty if neither is set.
    fs::path CutFolder() {
        wchar_t value[MAX_PATH] = {};

        if (GetEnvironmentVariableW(L"AUTO_SYNC_MENU_CUT", value, MAX_PATH) > 0) {
            return value;
        }

        if (GetEnvironmentVariableW(L"SIMOPTIX_DATA", value, MAX_PATH) > 0) {
            return fs::path(value) / L"auto_sync_menu_cut";
        }

        return {};
    }

    // The menu cut: its frames' first and last times, and its sound's (on the recording's clock).
    struct MenuCut {
        fs::path folder, sound;
        double first = 0, last = 0, soundStart = 0, rate = 0;
    };

    bool ReadMenuCut(const fs::path& folder, MenuCut* cut) {
        std::ifstream index(folder / "menu_cut.csv"), frames(folder / "menu.csv");
        std::string line;

        while (std::getline(index, line)) {
            const std::vector<std::string> v = Split(line);

            if (v.size() >= 4 && v[0] == "sound") {
                cut->soundStart = std::stod(v[1]);
                cut->rate = std::stod(v[2]);
            }
        }

        bool any = false;

        while (std::getline(frames, line)) {
            const double time = std::stod(Split(line).at(0));
            cut->first = any ? cut->first : time;
            cut->last = time;
            any = true;
        }

        cut->folder = folder;
        cut->sound = folder / "sound.f32";
        return any && cut->rate > 0 && Exists(cut->sound);
    }

    // What auto sync settled at, from its "auto sync done" line.
    struct Settled {
        bool hasSound = false;
        double soundLag = 0;     // s: the ticks after their highlights
        double correctionS = 0;  // what the game's sound moves by
        std::string line;
    };

    // The `n`th "auto sync done" line in the log (0: the first).
    Settled ReadSettled(const std::string& log, int n = 0) {
        Settled s;
        size_t at = log.find(kDone);

        for (int i = 0; i < n && at != std::string::npos; i++) {
            at = log.find(kDone, at + 1);
        }

        if (at == std::string::npos) {
            return s;
        }

        const size_t end = log.find_first_of("\r\n", at);
        s.line = log.substr(
            at + strlen(kDone),
            end == std::string::npos ? std::string::npos : end - at - strlen(kDone)
        );
        const char* sound = strstr(s.line.c_str(), "' has them ");
        double ms = 0;

        if (sound && sscanf_s(sound, "' has them %lf ms", &ms) == 1) {
            s.hasSound = true;
            s.soundLag = ms / 1000;
        }

        const char* moves = strstr(s.line.c_str(), "so it moves ");

        if (moves && sscanf_s(moves, "so it moves %lf ms", &ms) == 1) {
            s.correctionS = ms / 1000;
        }

        return s;
    }

    // The view's delay that the audio sources were last shifted by (ms), from the last "audio sync: ... now"
    // line. False if there is none.
    bool ReadDelay(const std::string& log, long long* delayMs) {
        bool found = false;

        for (size_t at = log.find(kShifted); at != std::string::npos; at = log.find(kShifted, at + 1)) {
            const size_t end = log.find_first_of("\r\n", at);
            const std::string line = log.substr(at, end == std::string::npos ? std::string::npos : end - at);
            const char* now = strstr(line.c_str(), " now ");
            long long ms = 0;

            if (now && strstr(now, "on top of their own offsets")
                && sscanf_s(now, " now %lld ms", &ms) == 1) {
                *delayMs = ms;
                found = true;
            }
        }

        return found;
    }

    // The source named `name` in a saved scene collection, as text: its object in the collection's list of
    // sources, at depth 2. The scene's items have the same names, deeper. Empty if there is none.
    std::string SavedSource(const std::string& json, const std::string& name) {
        std::vector<size_t> starts;
        std::string key;
        size_t found = 0;  // the depth of the source's object, once found

        for (size_t i = 0; i < json.size(); i++) {
            if (json[i] == '"') {
                const size_t end = json.find('"', i + 1);

                if (end == std::string::npos) {
                    return {};
                }

                const std::string text = json.substr(i + 1, end - i - 1);
                const size_t next = json.find_first_not_of(" \t\r\n", end + 1);

                if (next != std::string::npos && json[next] == ':') {
                    key = text;
                } else if (!found && key == "name" && text == name && starts.size() == 2) {
                    found = starts.size();
                }

                i = end;
            } else if (json[i] == '{') {
                starts.push_back(i);
            } else if (json[i] == '}' && !starts.empty()) {
                if (found && starts.size() == found) {
                    return json.substr(starts.back(), i - starts.back() + 1);
                }

                starts.pop_back();
            }
        }

        return {};
    }

    // The whole number after `"key":` in `object`. False if there is none.
    bool ReadNumber(const std::string& object, const std::string& key, long long* value) {
        const size_t at = object.find("\"" + key + "\"");

        if (at == std::string::npos) {
            return false;
        }

        const size_t colon = object.find(':', at);
        return colon != std::string::npos && sscanf_s(object.c_str() + colon + 1, " %lld", value) == 1;
    }

    struct Run {
        const char* name;
        double soundLagMs;
        bool again;  // the game's frames stop and start again after auto sync is done: it measures again
        long long ownOffsetMs;  // the game's sound's own sync offset
    };

    std::string Ms(double seconds) {
        char text[32];
        snprintf(text, sizeof(text), "%.0f ms", seconds * 1000);
        return text;
    }

    // The game's sound's offset as OBS saved it at the end: the view's delay and auto sync's last correction
    // alone, with its own offset kept under the plugin's shift.
    void CheckSavedOffset(const ObsCopy& obs, const std::string& log, const Settled& last, const Run& run) {
        long long delayMs = 0, syncNs = 0, shiftNs = 0;
        const std::string source = SavedSource(SavedSceneCollection(obs), kGameSound);
        const bool saved = ReadDelay(log, &delayMs) && ReadNumber(source, "sync", &syncNs)
                           && ReadNumber(source, kShiftKey, &shiftNs);

        if (!saved || !last.hasSound) {
            Check(false, "the game's sound's offset saved, with the delay and the plugin's shift");
            return;
        }

        const double wantS = static_cast<double>(delayMs) / 1000 + last.correctionS;
        const double syncS = static_cast<double>(syncNs) / 1e9;
        Check(
            std::fabs(syncS - wantS) <= kSavedToleranceS,
            "the game's sound saved at " + Ms(syncS) + ": the delay " + std::to_string(delayMs)
                + " ms and the correction " + Ms(last.correctionS) + ", without its own "
                + std::to_string(run.ownOffsetMs) + " ms"
        );

        Check(
            syncNs - shiftNs == run.ownOffsetMs * 1000000,
            "its own offset kept under the plugin's shift: " + Ms(static_cast<double>(syncNs - shiftNs) / 1e9)
        );
    }

    // One run: the fake game with the menu cut, and OBS until auto sync is done (twice with `again`).
    Settled RunOnce(
        const ObsCopy& obs,
        const fs::path& repo,
        const fs::path& out,
        const MenuCut& cut,
        const Run& run
    ) {
        printf("%s: sound lag %.0f ms\n", run.name, run.soundLagMs);
        const fs::path dir = out / run.name;
        std::error_code ec;
        fs::create_directories(dir / "appdata", ec);

        // The plan: the menus, from when the test sets startEvent. With `again`, the fake game restarts when
        // it sets restartEvent.
        const double menuS = std::min(kMenuMostS, cut.last - cut.first);
        char text[2048];
        snprintf(text, sizeof(text), "menu,0,%.6f,%.6f\n", menuS, cut.first);
        const std::string planText = text;

        const fs::path plan = dir / "plan.csv";
        Touch(plan, planText.c_str());

        const std::wstring stopEvent = L"Local\\SimOptix.AutoSyncE2E.Stop."
                                       + std::to_wstring(GetCurrentProcessId());
        const std::wstring startName = L"Local\\SimOptix.AutoSyncE2E.Start."
                                       + std::to_wstring(GetCurrentProcessId());
        const UniqueHandle startEvent(CreateEventW(nullptr, TRUE, FALSE, startName.c_str()));
        const std::wstring restartName = L"Local\\SimOptix.AutoSyncE2E.Restart."
                                         + std::to_wstring(GetCurrentProcessId());
        const UniqueHandle restartEvent(CreateEventW(nullptr, TRUE, FALSE, restartName.c_str()));
        // A quarter of the default image's sides, as the local files test: auto sync looks at grey copies
        // far smaller than that.
        snprintf(
            text,
            sizeof(text),
            "log=%s\nstop_event=%s\nwidth=1271\nheight=1042\nplan=%s\nstart_event=%s\nmenu_cut=%s\n"
            "menu_sound=%s\nmenu_sound_rate=%.0f\nmenu_sound_after=%.6f\nmenu_sound_lag_ms=%.1f\n"
            "restart_event=%s\nrestart_s=%.1f\n",
            (dir / "fake_openxr.log").string().c_str(),
            fs::path(stopEvent).string().c_str(),
            plan.string().c_str(),
            fs::path(startName).string().c_str(),
            cut.folder.string().c_str(),
            cut.sound.string().c_str(),
            cut.rate,
            cut.soundStart - cut.first,
            run.soundLagMs,
            run.again ? fs::path(restartName).string().c_str() : "",
            kRestartS
        );
        const fs::path scenario = dir / "fake_openxr.cfg";
        Touch(scenario, text);

        FakeGameProcess game;
        Settled settled;

        if (!LaunchFakeGame(
                dir,
                repo / "vr_mirror" / "e2e" / "bin" / "fake_openxr.exe",
                scenario,
                stopEvent,
                &game
            )) {
            Check(false, "the fake game (fake_openxr.exe) started");
            StopFakeGame(game);
            return settled;
        }

        ObsRun obsRun;
        obsRun.sources = {"simoptix_openxr_mirror"};
        obsRun.desktopAudio = true;
        obsRun.desktopAudioSyncNs = run.ownOffsetMs * 1000000;
        obsRun.localAppData = dir / "appdata";
        // The fake runtime, which only the Pimax source loads, with no game.
        obsRun.fakePvrScenario = dir / "fake_pvr.cfg";
        const std::string pvr = "log=" + (dir / "fake_pvr.log").string() + "\ngame_start_s=1e6\n";
        Touch(obsRun.fakePvrScenario, pvr.c_str());
        obsRun.holdUntilLogged = kDone;
        obsRun.holdUntilTimes = run.again ? 2 : 1;
        obsRun.holdUntilMs = static_cast<DWORD>(
            (kMeasuringMostS + menuS + (run.again ? kRestartS + menuS : 0) + 2) * 1000
        );
        // The menus start once auto sync measures, and the game restarts once it is done the first time.
        obsRun.cues.push_back({kMeasuring, [&] { SetEvent(startEvent.Get()); }});

        if (run.again) {
            obsRun.cues.push_back({kDone, [&] { SetEvent(restartEvent.Get()); }});
        }
        obsRun.holdMs = 1000;
        std::string log, error;
        const bool ran = RunObs(obs, obsRun, &log, &error);
        StopFakeGame(game);
        Touch(dir / "obs.log", log.c_str());

        Check(ran, ran ? "OBS ran and closed" : "OBS ran: " + error);
        settled = ReadSettled(log);
        Check(!settled.line.empty(), "auto sync done: " + settled.line);
        const double soundLag = run.soundLagMs / 1000;
        Check(
            settled.hasSound,
            "the game's sound found, its ticks " + Ms(settled.soundLag)
                + " after their highlights: the menus' own " + Ms(settled.soundLag - soundLag) + " and the "
                + Ms(soundLag) + " put in"
        );

        CheckSavedOffset(obs, log, run.again ? ReadSettled(log, 1) : settled, run);

        if (run.again) {
            const Settled second = ReadSettled(log, 1);
            Check(
                log.find("auto sync: measuring again (the game's frames started again changed)")
                    != std::string::npos,
                "auto sync measured again once the game's frames started again"
            );
            Check(
                second.hasSound && std::fabs(second.soundLag - settled.soundLag) <= kMovedToleranceS,
                "done again, within " + Ms(kMovedToleranceS) + " of the first: " + second.line
            );
        }

        return settled;
    }

}

int wmain() {
    if (!EndChildrenWithThisProcess()) {
        printf("warning: a run stopped midway may leave OBS or a fake running\n");
    }

    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    const fs::path repo = fs::path(self).parent_path().parent_path().parent_path().parent_path();
    const fs::path out = repo / "vr_mirror" / "e2e" / "out" / "auto_sync";

    MenuCut cut;
    const fs::path folder = CutFolder();

    if (folder.empty() || !ReadMenuCut(folder, &cut)) {
        printf("no menu cut: set SIMOPTIX_DATA, or AUTO_SYNC_MENU_CUT (export_menu_cut.py makes one)\n");
        return 2;
    }

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

    const Run runs[] = {
        {"sound_0", 0, false, -120},
        {"sound_60_again", 60, true, 0},
    };
    Settled settled[2];

    for (int i = 0; i < 2; i++) {
        settled[i] = RunOnce(obs, repo, out, cut, runs[i]);
    }

    printf("between the runs\n");
    const double moved = settled[1].soundLag - settled[0].soundLag;
    const double want = (runs[1].soundLagMs - runs[0].soundLagMs) / 1000;
    Check(
        settled[0].hasSound && settled[1].hasSound && std::fabs(moved - want) <= kMovedToleranceS,
        "the sound's lag moved " + Ms(moved) + ", within " + Ms(kMovedToleranceS) + " of " + Ms(want)
    );

    printf(
        "%s (%d failed). The runs' files: %ls\n",
        g_failures ? "FAILED" : "passed",
        g_failures,
        out.c_str()
    );
    return g_failures ? 1 : 0;
}
