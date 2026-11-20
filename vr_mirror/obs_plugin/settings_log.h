// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// SettingsLog: when a VR Mirror source's stabilization settings go into OBS's log, and the line that says
// them: Stabilization Method, Stabilization Strength (the level and the core's strength it gives), Zoom and
// the drawn pose method. Once when the source starts, and once after each change, when the new settings
// have held for kSettleS: a drag of the Zoom slider (100 steps a unit) gives one line, for where it stopped.
// Logic only: the time comes in, and the caller writes the line. Knows nothing about OBS's log, the view or
// threads.
#pragma once

#include "pipeline/pipeline_config.h"
#include "source_settings.h"

class SettingsLog {
public:
    static constexpr double kSettleS = 1;

    // The source started: the next Due gives its settings at once.
    void Restart() { restarted_ = true; }

    // Whether `settings`, with the drawn pose method the pipeline runs (`drawnPose`), go into the log at `t`
    // (seconds); then Line holds them.
    bool Due(const source_settings::SourceSettings& settings, DrawnPoseKind drawnPose, double t);
    // The settings as Due last gave them, in plain words (no newline).
    const char* Line() const { return line_; }

private:
    // What the line says.
    struct Logged {
        int method = -1;  // MirrorSettings::smoothing; -1: none yet
        int strengthLevel = 0;
        float strength = 0, zoom = 0;
        DrawnPoseKind drawnPose = DrawnPoseKind::kArrivalSlot;

        bool operator==(const Logged& o) const;
        bool operator!=(const Logged& o) const { return !(*this == o); }
    };

    void Format(const Logged& logged);

    bool restarted_ = false;
    Logged logged_;   // as last logged
    Logged pending_;  // a change not logged yet, and since when
    double pendingSince_ = 0;
    char line_[256] = {};
};
