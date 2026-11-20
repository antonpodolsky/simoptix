// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "settings_log.h"

#include <cstdio>

namespace {

    // The names the properties dialog shows.
    const char* MethodName(int method) {
        return method == MirrorSettings::kSmoothingOff       ? "Off"
               : method == MirrorSettings::kSmoothingInstant ? "Instant"
                                                             : "Look-ahead";
    }

    const char* DrawnPoseWords(DrawnPoseKind kind) {
        return kind == DrawnPoseKind::kSlotCheck     ? "the slot check"
               : kind == DrawnPoseKind::kCockpit     ? "the slot check and the cockpit correction"
               : kind == DrawnPoseKind::kReported    ? "the pose each frame comes with"
               : kind == DrawnPoseKind::kArrivalSlot ? "the slot each frame arrived for"
                                                     : "?";
    }

}

bool SettingsLog::Logged::operator==(const Logged& o) const {
    return method == o.method && strengthLevel == o.strengthLevel && strength == o.strength && zoom == o.zoom
           && drawnPose == o.drawnPose;
}

bool SettingsLog::Due(const source_settings::SourceSettings& settings, DrawnPoseKind drawnPose, double t) {
    Logged now;
    now.method = settings.mirror.smoothing;
    now.strengthLevel = settings.strengthLevel;
    now.strength = settings.mirror.strength;
    now.zoom = settings.mirror.zoom;
    now.drawnPose = drawnPose;

    // Back where it was logged before it held: nothing to say.
    if (!restarted_ && now == logged_) {
        pending_ = now;
        return false;
    }

    if (now != pending_) {
        pending_ = now;
        pendingSince_ = t;
    }

    if (!restarted_ && t - pendingSince_ < kSettleS) {
        return false;
    }

    restarted_ = false;
    logged_ = now;
    Format(now);
    return true;
}

void SettingsLog::Format(const Logged& logged) {
    std::snprintf(
        line_,
        sizeof(line_),
        "stabilization settings: Stabilization Method %s, Stabilization Strength %.1f (strength %.2f), "
        "Zoom %.2f, drawn pose from %s",
        MethodName(logged.method),
        logged.strengthLevel,
        logged.strength,
        logged.zoom,
        DrawnPoseWords(logged.drawnPose)
    );
}
