// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// SimOptix Setup's window as it looks: where everything goes at a display scale, and the drawing of
// everything in it but the buttons. The band holds the logo, the name, setup's version, what to close,
// and the main action or a run's progress. A row per part holds its icon tile, name, version and
// description, and while a run goes, its state. The numbers are setup_theme.h's. Knows nothing about
// what the buttons do or how the texts are decided.
#pragma once

#include <windows.h>

#include <array>
#include <string>
#include <vector>

#include "setup_theme.h"

// A part's place in a run of changes, which its row shows instead of its buttons.
enum class RunState { kNone, kWaiting, kBusy, kDone, kFailed };

// What a run does to each part.
enum class RunVerb { kInstall, kUpdate, kRepair, kUninstall };

struct RowView {
    bool installed = false;  // the icon tile is teal, and grey otherwise
    std::wstring version;    // after the name, empty when not installed
    RunState run = RunState::kNone;
    RunVerb verb = RunVerb::kInstall;
};

struct WindowView {
    std::wstring setupVersion;
    std::vector<std::wstring> reasons;  // what to close, one line each
    bool upToDate = false;              // the band says "Up to date" in place of the main action
    bool running = false;               // the band shows the run's progress in place of the main action
    std::wstring runLabel;              // "Installing Overlays"
    std::wstring runCount;              // "2 of 3"
    float barFraction = 0;              // how much of the run's bar is filled, 0 to 1
    float spinnerDeg = 0;               // where the spinner's arc starts
    std::array<RowView, 3> rows;        // in the order of kParts
};

// Every place in the window, in the device's pixels.
struct WindowLayout {
    UINT dpi = 96;
    SIZE client = {};
    int bandHeight = 0;
    std::array<int, 3> rowTops = {};
    int footerTop = 0;
    RECT main = {};   // the main action's button
    RECT close = {};  // the Close button
};

// `reasonCount`: the lines of what to close. A second one makes the band, and the window, 16 px taller.
WindowLayout LayOut(UINT dpi, size_t reasonCount);

void PaintWindow(HDC dc, const WindowLayout& layout, const ThemeFonts& fonts, const WindowView& view);

// The parts that move while a run goes, for redrawing only them.
RECT RunBarRect(const WindowLayout& layout);
RECT RunStateRect(const WindowLayout& layout, size_t row);

// The width `text` takes in `font`, in the device's pixels.
int MeasureText(HFONT font, const std::wstring& text);

// "Installing", "Updating", "Repairing", "Uninstalling".
const wchar_t* RunningWord(RunVerb verb);
