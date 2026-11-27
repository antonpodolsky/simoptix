// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Starting a private copy of simoptix_vr_mirror.exe against the fake runtime, finding its window, and
// closing it; and the fake game with the OpenXR layer's frames (fake_openxr.exe), which runs beside it.
// Also ending every process a test starts when the test ends, however it ends.
#pragma once

#include "common.h"
#include "shared/unique_handle.h"

// Puts this process in a job that Windows ends, with every process it starts afterwards, when this
// process ends: a test stopped midway (Ctrl+C, or a cancelled run on GitHub) leaves no mirror app, fake
// game or OBS running, holding its files. Call it first in wmain. False if Windows refused.
bool EndChildrenWithThisProcess();

struct ToolProcess {
    PROCESS_INFORMATION pi = {};
    HWND hwnd = nullptr;
};

// Starts `exe` in `dir` with the fake runtime's settings (`scenarioFile`), its log to tool.log and
// its frame trace to tool_frames.csv, and waits up to 10 s for its window.
bool Launch(const fs::path& dir, const fs::path& exe, const fs::path& scenarioFile, ToolProcess* out);
void Close(ToolProcess& p);

// fake_openxr.exe from `exe`, in `dir`, with its settings (`scenarioFile`), its output to fake_openxr.out,
// and this program's environment with each NAME=value of `set` in place of the same name's; it runs until
// StopFakeGame sets the event the settings name (`stopEvent`, made here).
struct FakeGameProcess {
    PROCESS_INFORMATION pi = {};
    UniqueHandle stop;
};

bool LaunchFakeGame(
    const fs::path& dir,
    const fs::path& exe,
    const fs::path& scenarioFile,
    const std::wstring& stopEvent,
    FakeGameProcess* out,
    const std::vector<std::wstring>& set = {}
);

void StopFakeGame(FakeGameProcess& p);
