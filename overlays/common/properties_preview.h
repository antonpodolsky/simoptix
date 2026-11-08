// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Properties preview: sizes OBS's Properties window for a source so its preview shows the source at a
// given size in screen pixels (for the G-meter: its size on the canvas, so the preview is 1:1).
//
// OBS's Properties window (OBSBasicProperties, OBS 32) is a dialog with a vertical splitter: the preview (its
// own native window) above, the settings below. OBS fits the source into the preview keeping its aspect, so a
// square source is shown at the preview's smaller side. OBS gives a source no handle to the window and its
// title is translated ("Properties for '<name>'", "Eigenschaften von „<name>“"), so it is found as the window
// that was just activated on the UI thread (the dialog, right after it opened) with the source's name in its
// title, and remembered for later fits while it stays open. The splitter shares a height change between its
// two panes, so the fit takes a few steps, one per call, each measuring the last.
//
// Knows nothing about what the source draws: only Win32 windows. UI thread only.
#pragma once

struct PreviewFit {
    void* dialog = nullptr;  // the Properties window found (an HWND), kept across fits
    int steps = 0;           // resizes made in this fit
    int lastWindowH = 0, lastPreviewH = 0;
    float gain = 0.5f;    // preview height change per window height change (measured)
    int bias = 0;         // extra pixels, if OBS's fit shows one pixel less than the preview's side
    bool logged = false;  // the preview's window was described in the log
};

enum class PreviewFitResult { NoWindow, Resizing, Fitted, GaveUp };

// One step: finds the Properties window of the source called `name` and resizes it so the preview's smaller
// side becomes `pixels` (+ fit.bias), keeping the window on its monitor. `shownPixels`: the size the source
// was last drawn at in a preview (0: not known), to confirm the fit.
PreviewFitResult FitPropertiesPreview(const char* name, int pixels, int shownPixels, PreviewFit& fit);

// Starts a new fit, keeping the window found before.
void RestartPreviewFit(PreviewFit& fit);
