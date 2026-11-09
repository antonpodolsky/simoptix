// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// A run of changes, installing or uninstalling parts one after the other, on a thread of its own, so that
// the window keeps drawing its progress meanwhile. The thread tells the window of each step with a posted
// message (below). Knows nothing about how the window shows them.
#pragma once

#include <windows.h>

#include <string>
#include <thread>
#include <vector>

#include "setup_facts.h"
#include "setup_parts.h"

enum class Change { kInstall, kUninstall };

// wParam: the index of the part in the run's list, which has started.
constexpr UINT kRunPartStarted = WM_APP + 1;

// wParam: the index of the part, which is done. lParam: nullptr when it succeeded, or a std::wstring*
// with the reason it failed, which the window takes and frees (TakeRunError).
constexpr UINT kRunPartDone = WM_APP + 2;

// The run has ended. The window then calls Join.
constexpr UINT kRunFinished = WM_APP + 3;

// Takes the reason a part failed from a kRunPartDone's lParam. Empty when the part succeeded.
std::wstring TakeRunError(LPARAM lParam);

class ChangeRun {
public:
    ChangeRun() = default;
    ChangeRun(const ChangeRun&) = delete;
    ChangeRun& operator=(const ChangeRun&) = delete;
    ~ChangeRun();

    // Starts changing `parts` in order, posting the messages above to `window`. One run at a time.
    void Start(HWND window, Change change, const std::vector<PartId>& parts, const SystemFacts& system);

    // Waits for the run's thread to end. Safe to call twice.
    void Join();

private:
    std::thread thread_;
};
