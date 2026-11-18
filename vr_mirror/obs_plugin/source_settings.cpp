// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "source_settings.h"

#include <algorithm>

namespace source_settings {

    void UpgradeSavedSettings(obs_data_t* s) {
        if (obs_data_has_user_value(s, "smoothing")) {
            obs_data_set_int(s, kMethodKey, obs_data_get_int(s, "smoothing"));
            obs_data_erase(s, "smoothing");
        }

        if (obs_data_has_user_value(s, "smoothing_strength")) {
            obs_data_set_double(s, kStrengthKey, obs_data_get_double(s, "smoothing_strength"));
            obs_data_erase(s, "smoothing_strength");
        }

        if (obs_data_get_int(s, kMethodKey) > MirrorSettings::kSmoothingLookahead) {
            obs_data_set_int(s, kMethodKey, MirrorSettings::kSmoothingLookahead);
        }
    }

    MirrorSettings MirrorSettingsOf(obs_data_t* s, bool* debug) {
        MirrorSettings st;
        st.smoothing = std::clamp(
            static_cast<int>(obs_data_get_int(s, kMethodKey)),
            MirrorSettings::kSmoothingOff,
            MirrorSettings::kSmoothingLookahead
        );
        st.strength = std::clamp(
            static_cast<float>(obs_data_get_double(s, kStrengthKey)),
            kMinStrength,
            kMaxStrength
        );

        st.zoom = std::clamp(float(obs_data_get_double(s, "zoom")), 1.0f, 3.0f);
        st.drawnPose = Stabilizes(st) ? kDrawnPose : 0;  // off: no slot check, no thumbnails
        *debug = obs_data_get_bool(s, "debug");
        return st;
    }

    void SetDefaults(obs_data_t* s) {
        obs_data_set_default_int(s, kMethodKey, MirrorSettings::kSmoothingLookahead);
        obs_data_set_default_double(s, kStrengthKey, 1.0);
        obs_data_set_default_double(s, "zoom", kDefaultZoom);
        obs_data_set_default_bool(s, "auto_record", false);
        obs_data_set_default_bool(s, "debug", false);
    }

    bool MethodOn(obs_data_t* settings) {
        return !settings || obs_data_get_int(settings, kMethodKey) != MirrorSettings::kSmoothingOff;
    }

}
