// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// What the window shows of a run of changes, from its start until its results are put away: which row
// waits, turns its spinner, or shows its tick, what the band's label and count say, how full the bar is
// as it eases to each new step, and the reasons the parts that failed give. Logic only: the time comes in
// as a parameter, and it reads nothing from Windows. The window owns the thread and the timers.
#pragma once

#include <array>
#include <string>
#include <vector>

#include "setup_look.h"
#include "setup_run.h"
#include "setup_status.h"

class RunProgress {
public:
    // The run starts: each of `parts` waits. `statuses` say what each part's change is (install, update
    // or repair). `nowMs` is any millisecond clock, the same in every call.
    void Start(
        Change change,
        const std::vector<PartId>& parts,
        const std::array<PartStatus, 3>& statuses,
        unsigned long long nowMs,
        WindowView* view
    );

    // The part at `index` in the run has started.
    void PartStarted(size_t index, WindowView* view) const;

    // The part at `index` is done, `error` empty when it succeeded. The bar starts easing to its new fill.
    void PartDone(size_t index, const std::wstring& error, unsigned long long nowMs, WindowView* view);

    // The run has ended. A part it didn't report as done counts as failed. Returns every part's reason
    // for failing, one line each, or empty when all succeeded.
    std::wstring Finish(WindowView* view);

    // The results have been shown: the rows show their buttons again.
    void Clear(WindowView* view);

    // Turns the spinner and eases the bar to `nowMs`. True while anything still moves.
    bool Animate(unsigned long long nowMs, WindowView* view) const;

private:
    std::vector<PartId> parts_;
    size_t done_ = 0;
    std::wstring errors_;
    unsigned long long spinStartMs_ = 0;
    unsigned long long barStartMs_ = 0;
    float barFrom_ = 0;
    float barTo_ = 0;
};
