// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "local_files_obs.h"

#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <fstream>
#include <map>
#include <sstream>

#include "local_files_folder.h"
#include "shared/unique_handle.h"

namespace {

    constexpr const char* kName = "SimOptixE2E";  // the profile and the scene collection
    constexpr DWORD kStartMs = 30000;
    constexpr DWORD kCloseMs = 30000;

    // What OBS shows for each source id, as the plugins name them.
    const char* DisplayName(const std::string& id) {
        if (id == "simoptix_pimax_mirror") {
            return "SimOptix: Pimax Mirror";
        }

        if (id == "simoptix_openxr_mirror") {
            return "SimOptix: OpenXR Mirror";
        }

        return id.c_str();
    }

    std::string Uuid(int i) {
        char uuid[40];
        snprintf(uuid, sizeof(uuid), "00000000-0000-4000-8000-%012d", i);
        return uuid;
    }

    bool Write(const fs::path& path, const std::string& text) {
        std::ofstream file(path, std::ios::binary);
        file << text;
        return file.good();
    }

    std::string Read(const fs::path& path) {
        std::ifstream file(path, std::ios::binary);
        std::stringstream text;
        text << file.rdbuf();
        return text.str();
    }

    // A scene that shows each source in `sources` at the canvas's top left, with Debug on, Instant
    // smoothing (no output delay) and race auto-record off.
    std::string SceneCollection(const std::vector<std::string>& sources) {
        std::string items, list;

        for (size_t i = 0; i < sources.size(); i++) {
            const int id = static_cast<int>(i) + 1;
            items += std::string(i ? "," : "") + "{\"name\":\"" + DisplayName(sources[i])
                     + "\",\"source_uuid\":\"" + Uuid(id)
                     + "\",\"visible\":true,\"locked\":false,\"id\":" + std::to_string(id)
                     + ",\"pos\":{\"x\":0.0,\"y\":0.0},\"scale\":{\"x\":1.0,\"y\":1.0},\"rot\":0.0,\"align\":"
                       "5}";
            list += ",{\"name\":\"" + std::string(DisplayName(sources[i])) + "\",\"uuid\":\"" + Uuid(id)
                    + "\",\"id\":\"" + sources[i] + "\",\"versioned_id\":\"" + sources[i]
                    + "\",\"enabled\":true,\"settings\":{\"debug\":true,\"smoothing\":1,\"auto_record\":"
                      "false}}";
        }

        return std::string("{\"name\":\"") + kName
               + "\",\"current_scene\":\"Scene\",\"current_program_scene\":\"Scene\","
                 "\"scene_order\":[{\"name\":\"Scene\"}],\"sources\":[{\"name\":\"Scene\",\"uuid\":\""
               + Uuid(0) + "\",\"id\":\"scene\",\"versioned_id\":\"scene\",\"enabled\":true,\"settings\":{"
               + "\"id_counter\":" + std::to_string(sources.size()) + ",\"custom_size\":false,\"items\":["
               + items + "]}}" + list + "]}";
    }

    // OBS's settings for one run: no first-run questions, a 1280x720 canvas at 60 fps, and the scene.
    bool WriteSettings(const fs::path& dir, const std::vector<std::string>& sources) {
        const fs::path config = dir / "config" / "obs-studio";
        const fs::path profile = config / "basic" / "profiles" / kName;
        const fs::path scenes = config / "basic" / "scenes";
        std::error_code ec;
        fs::remove_all(dir / "config", ec);
        fs::create_directories(profile, ec);
        fs::create_directories(scenes, ec);

        const std::string name = kName;
        const bool written = Write(
                                 config / "global.ini",
                                 "[General]\r\nLastVersion=537001986\r\n"
                             )  // 32.2.2: no "what's new"
                             && Write(
                                 config / "user.ini",
                                 "[General]\r\nFirstRun=true\r\n[Basic]\r\nProfile=" + name
                                     + "\r\nProfileDir=" + name + "\r\nSceneCollection=" + name
                                     + "\r\nSceneCollectionFile=" + name + ".json\r\n"
                             )
                             && Write(
                                 profile / "basic.ini",
                                 "[General]\r\nName=" + name
                                     + "\r\n[Video]\r\nBaseCX=1280\r\nBaseCY=720\r\nOutputCX="
                                       "1280\r\nOutputCY=720\r\nFPSType=0\r\n"
                                       "FPSCommon=60\r\n[Output]\r\nMode=Simple\r\n"
                             )
                             && Write(scenes / (name + ".json"), SceneCollection(sources));

        return written;
    }

    struct WindowSearch {
        DWORD pid;
        HWND found;
    };

    // OBS's main window: visible, with a title that starts with "OBS".
    HWND MainWindowOf(DWORD pid) {
        WindowSearch search = {pid, nullptr};

        EnumWindows(
            [](HWND hwnd, LPARAM param) -> BOOL {
                WindowSearch* s = reinterpret_cast<WindowSearch*>(param);
                DWORD owner = 0;
                GetWindowThreadProcessId(hwnd, &owner);
                wchar_t title[16] = {};
                GetWindowTextW(hwnd, title, 16);

                if (owner == s->pid && IsWindowVisible(hwnd) && wcsncmp(title, L"OBS", 3) == 0) {
                    s->found = hwnd;
                    return FALSE;
                }

                return TRUE;
            },
            reinterpret_cast<LPARAM>(&search)
        );

        return search.found;
    }

    size_t Count(const std::string& text, const char* what) {
        size_t n = 0;

        for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) {
            n++;
        }

        return n;
    }

    // The newest log in the portable OBS's logs folder, as text.
    std::string NewestLog(const fs::path& dir) {
        const fs::path logs = dir / "config" / "obs-studio" / "logs";
        std::error_code ec;
        fs::path newest;

        for (const fs::directory_entry& entry : fs::directory_iterator(logs, ec)) {
            if (newest.empty() || entry.path().filename() > newest.filename()) {
                newest = entry.path();
            }
        }

        return newest.empty() ? std::string() : Read(newest);
    }

    bool CopyFileTo(const fs::path& from, const fs::path& to, std::string* error) {
        std::error_code ec;
        fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);

        if (ec) {
            *error = "could not copy " + from.string() + ": " + ec.message() + " (build it first)";
            return false;
        }

        return true;
    }

}

std::wstring EnvironmentWith(const std::vector<std::wstring>& set) {
    std::map<std::wstring, std::wstring> variables;  // by upper-case name, as Windows compares them

    auto add = [&](const std::wstring& entry) {
        const size_t equals = entry.find(L'=', 1);  // names like "=C:" start with '='
        std::wstring key = entry.substr(0, equals);
        std::transform(key.begin(), key.end(), key.begin(), towupper);
        variables[key] = entry;
    };

    wchar_t* const block = GetEnvironmentStringsW();

    for (const wchar_t* e = block; *e; e += wcslen(e) + 1) {
        add(e);
    }

    FreeEnvironmentStringsW(block);

    for (const std::wstring& entry : set) {
        add(entry);
    }

    std::wstring environment;

    for (const auto& variable : variables) {
        environment += variable.second + L'\0';
    }

    return environment + L'\0';
}

bool PrepareObs(const fs::path& repo, const fs::path& out, ObsCopy* obs, std::string* error) {
    wchar_t installed[MAX_PATH] = L"C:\\Program Files\\obs-studio";
    GetEnvironmentVariableW(L"OBS_DIR", installed, MAX_PATH);  // keeps the default if unset
    obs->dir = repo / "vr_mirror" / "e2e" / "out" / "obs_portable";
    obs->plugins = out / "plugins";
    obs->pluginData = out / "plugin_data";
    std::error_code ec;

    if (!Exists(obs->dir / "bin" / "64bit" / "obs64.exe")) {
        if (!fs::exists(fs::path(installed) / "bin" / "64bit" / "obs64.exe", ec)) {
            *error = "no OBS in " + fs::path(installed).string() + " (OBS_DIR names another)";
            return false;
        }

        // Copied whole under another name first, so a copy cut short is never taken for a finished one.
        printf("copying OBS from %ls to %ls (once)\n", installed, obs->dir.c_str());
        const fs::path partial = obs->dir.wstring() + L".partial";
        fs::remove_all(partial, ec);
        fs::copy(installed, partial, fs::copy_options::recursive, ec);

        if (ec) {
            *error = "could not copy OBS: " + ec.message();
            return false;
        }

        fs::rename(partial, obs->dir, ec);

        if (ec) {
            *error = "could not rename the OBS copy: " + ec.message();
            return false;
        }
    }

    fs::create_directories(obs->plugins, ec);
    fs::create_directories(obs->pluginData / "simoptix_vr_mirror", ec);
    fs::create_directories(obs->pluginData / "simoptix_overlays", ec);

    const fs::path e2eBin = repo / "vr_mirror" / "e2e" / "bin";

    return CopyFileTo(e2eBin / "libPVRClient64.dll", obs->dir / "bin" / "64bit" / "libPVRClient64.dll", error)
           && CopyFileTo(
               repo / "vr_mirror" / "obs_plugin" / "bin" / "simoptix_vr_mirror.dll",
               obs->plugins / "simoptix_vr_mirror.dll",
               error
           )
           && CopyFileTo(
               repo / "overlays" / "obs_plugin" / "bin" / "simoptix_overlays.dll",
               obs->plugins / "simoptix_overlays.dll",
               error
           );
}

bool RunObs(const ObsCopy& obs, const ObsRun& run, std::string* log, std::string* error) {
    if (!WriteSettings(obs.dir, run.sources)) {
        *error = "could not write OBS's settings";
        return false;
    }

    const fs::path bin = obs.dir / "bin" / "64bit";
    const fs::path exe = bin / "obs64.exe";
    std::wstring environment = EnvironmentWith({
        L"LOCALAPPDATA=" + run.localAppData.wstring(),
        L"OBS_PLUGINS_PATH=" + obs.plugins.wstring(),
        L"OBS_PLUGINS_DATA_PATH=" + obs.pluginData.wstring(),
        L"SIMOPTIX_FAKE_PVR_SCENARIO=" + run.fakePvrScenario.wstring(),
        L"SIMOPTIX_VR_MIRROR_SERVER_EXE=obs64.exe",  // the fake runtime's frame signal is OBS's own
    });
    std::wstring
        command = L"\"" + exe.wstring()
                  + L"\" --portable --disable-shutdown-check --disable-updater --disable-missing-files-check";
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
        bin.c_str(),
        &startup,
        &process
    );

    if (!started) {
        *error = "OBS didn't start: " + std::to_string(GetLastError());
        return false;
    }

    const UniqueHandle processHandle(process.hProcess);
    const UniqueHandle threadHandle(process.hThread);

    // Each source logs "started (source shown)" once its frame source runs.
    bool sourcesStarted = false;

    for (DWORD waited = 0; waited < kStartMs && !sourcesStarted; waited += 250) {
        Sleep(250);
        sourcesStarted = Count(NewestLog(obs.dir), "started (source shown)") >= run.sources.size();
    }

    if (sourcesStarted) {
        Sleep(run.holdMs);
    }

    const HWND window = MainWindowOf(process.dwProcessId);

    if (window) {
        PostMessageW(window, WM_CLOSE, 0, 0);
    }

    const bool closed = window && WaitForSingleObject(processHandle.Get(), kCloseMs) == WAIT_OBJECT_0;

    if (!closed) {
        TerminateProcess(processHandle.Get(), 1);
        WaitForSingleObject(processHandle.Get(), kCloseMs);
    }

    *log = NewestLog(obs.dir);

    if (!sourcesStarted) {
        *error = "the sources didn't start within 30 s";
        return false;
    }

    if (!closed) {
        *error = "OBS didn't close within 30 s, and was ended";
        return false;
    }

    return true;
}
