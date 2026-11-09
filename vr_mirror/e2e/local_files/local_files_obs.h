// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The local files test's own OBS: a copy of the installed OBS in vr_mirror\e2e\out\obs_portable\, run in
// OBS's portable mode. Its settings live beside it, made fresh for each run, so the user's own OBS settings
// and scenes stay untouched. Portable mode also skips the plugins installed in C:\ProgramData. OBS loads the
// two plugins from this build instead (OBS_PLUGINS_PATH), and the Pimax source finds the fake runtime
// (fake_pvr) beside obs64.exe and its frame signal in OBS's own process (SIMOPTIX_VR_MIRROR_SERVER_EXE).
// Knows nothing about the files the plugins write.
#pragma once

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct ObsCopy {
    fs::path dir;         // the portable OBS: bin\64bit\obs64.exe and its config folder
    fs::path plugins;     // the plugins of this build, copied for the run
    fs::path pluginData;  // their (empty) data folders
};

// Copies the installed OBS (OBS_DIR, else C:\Program Files\obs-studio) into `repo`'s
// vr_mirror\e2e\out\obs_portable\ if it isn't there yet, the fake runtime beside its obs64.exe, and the two
// plugins into `out`. False, with the reason, if one is missing.
bool PrepareObs(const fs::path& repo, const fs::path& out, ObsCopy* obs, std::string* error);

struct ObsRun {
    std::vector<std::string> sources;  // the source ids the scene shows, each with Debug on
    fs::path localAppData;             // %LOCALAPPDATA% for OBS and the plugins
    fs::path fakePvrScenario;          // the fake runtime's settings (SIMOPTIX_FAKE_PVR_SCENARIO)
    DWORD holdMs = 5000;               // how long OBS runs once every source has started
};

// Runs OBS once with fresh settings and a scene that shows `run.sources`, then closes it as its close
// button does. `log` gets OBS's log. False, with the reason, if OBS didn't start, the sources didn't start
// within 30 s, or OBS didn't close within 30 s (then it is ended).
bool RunObs(const ObsCopy& obs, const ObsRun& run, std::string* log, std::string* error);

// The program's environment with `set` (NAME=value) in place of the same names, as CreateProcessW takes it.
std::wstring EnvironmentWith(const std::vector<std::wstring>& set);
