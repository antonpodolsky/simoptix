// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "setup_progress.h"

#include <cmath>

namespace {

    RunVerb VerbOf(Change change, PartAction action) {
        if (change == Change::kUninstall) {
            return RunVerb::kUninstall;
        }

        switch (action) {
            case PartAction::kUpdate:
                return RunVerb::kUpdate;
            case PartAction::kRepair:
                return RunVerb::kRepair;
            case PartAction::kInstall:
            case PartAction::kNone:
                break;
        }
        return RunVerb::kInstall;
    }

    float EaseOutCubic(float t) {
        const float rest = 1 - t;
        return 1 - rest * rest * rest;
    }

    RowView& RowOf(PartId part, WindowView* view) {
        return view->rows[static_cast<size_t>(part)];
    }

    std::wstring FailureLine(PartId part, const std::wstring& reason) {
        return std::wstring(L"\n") + PartOf(part).shownName + L": " + reason;
    }

}

void RunProgress::Start(
    Change change,
    const std::vector<PartId>& parts,
    const std::array<PartStatus, 3>& statuses,
    unsigned long long nowMs,
    WindowView* view
) {
    parts_ = parts;
    done_ = 0;
    errors_.clear();
    spinStartMs_ = nowMs;
    barStartMs_ = nowMs;
    barFrom_ = barTo_ = 0;

    for (const PartId part : parts_) {
        RowView& row = RowOf(part, view);
        row.run = RunState::kWaiting;
        row.verb = VerbOf(change, statuses[static_cast<size_t>(part)].action);
    }

    view->running = true;
    view->barFraction = 0;
    view->runLabel.clear();
    view->runCount.clear();
}

void RunProgress::PartStarted(size_t index, WindowView* view) const {
    const PartId part = parts_[index];
    RowView& row = RowOf(part, view);
    row.run = RunState::kBusy;

    view->runLabel = std::wstring(RunningWord(row.verb)) + L" " + PartOf(part).shortName;
    view->runCount = std::to_wstring(index + 1) + L" of " + std::to_wstring(parts_.size());
}

void RunProgress::PartDone(
    size_t index,
    const std::wstring& error,
    unsigned long long nowMs,
    WindowView* view
) {
    const PartId part = parts_[index];
    RowOf(part, view).run = error.empty() ? RunState::kDone : RunState::kFailed;

    if (!error.empty()) {
        errors_ += FailureLine(part, error);
    }

    ++done_;
    barFrom_ = view->barFraction;
    barTo_ = static_cast<float>(done_) / static_cast<float>(parts_.size());
    barStartMs_ = nowMs;
}

std::wstring RunProgress::Finish(WindowView* view) {
    for (const PartId part : parts_) {
        RowView& row = RowOf(part, view);

        if (row.run == RunState::kWaiting || row.run == RunState::kBusy) {
            row.run = RunState::kFailed;
            errors_ += FailureLine(part, L"stopped by an unexpected error.");
        }
    }

    return errors_;
}

void RunProgress::Clear(WindowView* view) {
    for (RowView& row : view->rows) {
        row.run = RunState::kNone;
    }

    view->running = false;
    parts_.clear();
}

bool RunProgress::Animate(unsigned long long nowMs, WindowView* view) const {
    const float turnMs = static_cast<float>(theme::kSpinnerTurnMs);
    view->spinnerDeg = std::fmod(static_cast<float>(nowMs - spinStartMs_), turnMs) / turnMs * 360;

    const float t = std::fmin(static_cast<float>(nowMs - barStartMs_) / theme::kBarEaseMs, 1.0f);
    view->barFraction = barFrom_ + (barTo_ - barFrom_) * EaseOutCubic(t);

    bool spinning = false;

    for (const RowView& row : view->rows) {
        spinning = spinning || row.run == RunState::kBusy;
    }

    return spinning || t < 1;
}
