// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// AutoSyncValues: what auto sync found for a frame source, kept in VR Mirror's OBS config folder
// (auto_sync.json, one entry per frame source), so a new OBS session starts in step and measures again
// from there. Thin glue around OBS's settings files. Any one thread at a time.
#pragma once

#include <string>

struct AutoSyncValues {
    bool hasSound = false;
    std::string gameSound;       // the audio source with the game's sound
    double soundCorrection = 0;  // s: on top of the output delay, for that source
};

// The values kept for frame source `frameSource` (FrameSource::Name); none kept: all false.
AutoSyncValues LoadAutoSyncValues(const char* frameSource);
void SaveAutoSyncValues(const char* frameSource, const AutoSyncValues& values);
