// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Starting a private copy of simoptix_vr_mirror.exe against the fake runtime, finding its window, and
// closing it; and the fake game with the OpenXR layer's frames (fake_openxr.exe), which runs beside it.
#pragma once

#include "common.h"
#include "shared/unique_handle.h"

struct ToolProcess {
    PROCESS_INFORMATION pi = {};
    HWND hwnd = nullptr;
};

// Starts `exe` in `dir` with the fake runtime's settings (`scenarioFile`), its log to tool.log and
// its frame trace to tool_frames.csv, and waits up to 10 s for its window.
bool Launch(const fs::path& dir, const fs::path& exe, const fs::path& scenarioFile, ToolProcess* out);
void Close(ToolProcess& p);

// fake_openxr.exe from `exe`, in `dir`, with its settings (`scenarioFile`), its output to fake_openxr.out;
// it runs until StopFakeGame sets the event the settings name (`stopEvent`, made here).
struct FakeGameProcess {
    PROCESS_INFORMATION pi = {};
    UniqueHandle stop;
};

bool LaunchFakeGame(
    const fs::path& dir,
    const fs::path& exe,
    const fs::path& scenarioFile,
    const std::wstring& stopEvent,
    FakeGameProcess* out
);

void StopFakeGame(FakeGameProcess& p);
