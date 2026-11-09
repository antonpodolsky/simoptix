// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "setup_run.h"

#include <memory>

#include "setup_install.h"

namespace {

    constexpr DWORD kRepostWaitMs = 50;

    // Runs on the run's thread.
    void RunChanges(HWND window, Change change, const std::vector<PartId>& parts, const SystemFacts& system) {
        for (size_t i = 0; i < parts.size(); ++i) {
            // A lost start only leaves the row waiting until its result comes.
            (void)PostMessageW(window, kRunPartStarted, i, 0);

            std::wstring error;
            const bool done = change == Change::kInstall ? InstallPart(parts[i], system, &error)
                                                         : RemovePart(parts[i], &error);
            std::unique_ptr<std::wstring> reason = done ? nullptr : std::make_unique<std::wstring>(error);

            // The window takes the reason back from lParam (TakeRunError). If the message can't be
            // posted, the reason stays here and is freed.
            if (PostMessageW(window, kRunPartDone, i, reinterpret_cast<LPARAM>(reason.get()))) {
                (void)reason.release();
            }
        }
    }

}

std::wstring TakeRunError(LPARAM lParam) {
    const std::unique_ptr<std::wstring> reason(reinterpret_cast<std::wstring*>(lParam));
    return reason ? *reason : std::wstring();
}

ChangeRun::~ChangeRun() {
    Join();
}

void ChangeRun::Start(
    HWND window,
    Change change,
    const std::vector<PartId>& parts,
    const SystemFacts& system
) {
    Join();

    thread_ = std::thread([window, change, parts, system] {
        // An exception escaping a thread would end setup on the spot, halfway through a change. The window
        // hears that the run ended either way.
        try {
            RunChanges(window, change, parts, system);
        } catch (...) {
        }

        // Without this message the window would wait for the run forever. Posting fails only while its
        // queue is full, so it is tried again until the window takes it, or is gone.
        while (!PostMessageW(window, kRunFinished, 0, 0) && IsWindow(window)) {
            Sleep(kRepostWaitMs);
        }
    });
}

void ChangeRun::Join() {
    if (thread_.joinable()) {
        thread_.join();
    }
}
