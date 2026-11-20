// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The settings of a VR Mirror source as OBS saves them in the scene: their keys, their defaults, the
// keys of scenes saved by older builds, and the core's MirrorSettings they give (SourceSettings). Knows
// nothing about the properties dialog, the view or threads.
#pragma once

#include <obs-data.h>

#include "common/settings.h"

namespace source_settings {

    // The source's core settings are the smoothing, its strength and the zoom; the drawn pose method is
    // fixed here. A new source starts on Instant at Stabilization Strength 3 and Zoom 1.3.
    constexpr int kDefaultMethod = MirrorSettings::kSmoothingInstant;
    constexpr float kDefaultZoom = 1.3f;
    constexpr int kDrawnPose = 2;  // the slot check plus the cockpit correction below 90 fps

    // Stabilization Strength is a level from 0 to 10. Each level gives the core's strength
    // (MirrorSettings::strength, 1 each smoothing's own tuning). 0 smooths nothing (strength 0): the horizon
    // is still levelled, and the method keeps its delay. From 1 up the steps are even on a log scale on each
    // side of the middle, kTunedStrengthLevel: 1 is kMinStrength, 5 is 1 and 10 is kMaxStrength.
    constexpr int kNoSmoothingLevel = 0, kLightestStrengthLevel = 1;
    constexpr int kTunedStrengthLevel = 5, kMaxStrengthLevel = 10;
    constexpr int kDefaultStrengthLevel = 3;  // a factor of 0.5
    // Measured with stage_replay at zoom 1.25: at strength 2 Look-ahead still never stopped at an edge on the
    // racing motion; at 3 it did on 2.8% of its frames, and its jitter rose from 0.001 to 0.33 deg, because
    // it sees only half a second ahead (findings\obs_plugin.md).
    constexpr float kMinStrength = 0.25f, kMaxStrength = 2.0f;

    // The settings' keys: Stabilization Method (MirrorSettings::kSmoothing*) and Stabilization Strength's
    // level.
    constexpr const char* kMethodKey = "stabilization_method";
    constexpr const char* kStrengthKey = "stabilization_strength";

    // A source's settings as its view takes them: the core's, the Stabilization Strength level that gave
    // their strength (for the log), and Debug.
    struct SourceSettings {
        MirrorSettings mirror;
        int strengthLevel = kDefaultStrengthLevel;
        bool debug = false;  // the frame-number stamp and the frame trace together
    };

    // A scene saved before the method was renamed keeps it: "smoothing" moves to kMethodKey. One saved with
    // the dropped Smoothest (3) shows Look-ahead in the list, not an empty choice.
    void UpgradeSavedSettings(obs_data_t* s);

    // kMethodKey, kStrengthKey. "zoom": the crop factor (1 to 3). "debug": the frame-number stamp and the
    // frame trace together.
    SourceSettings SourceSettingsOf(obs_data_t* s);

    void SetDefaults(obs_data_t* s);

    // Whether the method in the dialog (`settings`) stabilizes; true without settings (the default does).
    bool MethodOn(obs_data_t* settings);

}
