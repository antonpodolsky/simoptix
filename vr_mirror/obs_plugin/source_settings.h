// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The settings of a VR Mirror source as OBS saves them in the scene: their keys, their defaults, the
// keys of scenes saved by older builds, and the core's MirrorSettings they give. Knows nothing about the
// properties dialog, the view or threads.
#pragma once

#include <obs-data.h>

#include "common/settings.h"

namespace source_settings {

    // The source's core settings are the smoothing, its strength and the zoom; the drawn pose method is
    // fixed here. 1920x1080 a little above native at Render Quality 1.0 (README.md, "Recording at the
    // source's native resolution")
    constexpr float kDefaultZoom = 1.25f;
    // Strength: 1 is each smoothing's own tuning. Measured with stage_replay at zoom 1.25: at 2 Look-ahead
    // still never stopped at an edge on the racing motion; at 3 it did on 2.8% of its frames, and its jitter
    // rose from 0.001 to 0.33 deg, because it sees only half a second ahead (findings\obs_plugin.md).
    constexpr float kMinStrength = 0.25f, kMaxStrength = 2.0f;
    constexpr int kDrawnPose = 2;  // the slot check plus the cockpit correction below 90 fps

    // The settings' keys: Stabilization Method (MirrorSettings::kSmoothing*) and Stabilization Strength.
    constexpr const char* kMethodKey = "stabilization_method";
    constexpr const char* kStrengthKey = "stabilization_strength";

    // A scene saved before the two settings were renamed keeps its values: "smoothing" and
    // "smoothing_strength" move to the new keys. One saved with the dropped Smoothest (3) shows
    // Look-ahead in the list, not an empty choice.
    void UpgradeSavedSettings(obs_data_t* s);

    // kMethodKey, kStrengthKey. "zoom": the crop factor (1 to 3). "debug": the frame-number stamp and the
    // frame trace together.
    MirrorSettings MirrorSettingsOf(obs_data_t* s, bool* debug);

    void SetDefaults(obs_data_t* s);

    // Whether the method in the dialog (`settings`) stabilizes; true without settings (the default does).
    bool MethodOn(obs_data_t* settings);

}
