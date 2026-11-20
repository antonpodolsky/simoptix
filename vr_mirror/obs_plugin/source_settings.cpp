// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "source_settings.h"

#include <algorithm>
#include <cmath>

namespace source_settings {

    namespace {

        // The strength at `level`: 0 at kNoSmoothingLevel; below kTunedStrengthLevel, even steps on a log
        // scale from 1 down to kMinStrength at kLightestStrengthLevel; above it, from 1 up to kMaxStrength.
        float StrengthOfLevel(int level) {
            if (level <= kNoSmoothingLevel) {
                return 0;
            }

            const int steps = level - kTunedStrengthLevel;

            if (steps < 0) {
                const float stepsDown = static_cast<float>(kTunedStrengthLevel - kLightestStrengthLevel);
                return std::pow(kMinStrength, static_cast<float>(-steps) / stepsDown);
            }

            const float stepsUp = static_cast<float>(kMaxStrengthLevel - kTunedStrengthLevel);
            return std::pow(kMaxStrength, static_cast<float>(steps) / stepsUp);
        }

    }

    void UpgradeSavedSettings(obs_data_t* s) {
        if (obs_data_has_user_value(s, "smoothing")) {
            obs_data_set_int(s, kMethodKey, obs_data_get_int(s, "smoothing"));
            obs_data_erase(s, "smoothing");
        }

        if (obs_data_get_int(s, kMethodKey) > MirrorSettings::kSmoothingLookahead) {
            obs_data_set_int(s, kMethodKey, MirrorSettings::kSmoothingLookahead);
        }
    }

    SourceSettings SourceSettingsOf(obs_data_t* s) {
        SourceSettings source;
        MirrorSettings& st = source.mirror;
        st.smoothing = std::clamp(
            static_cast<int>(obs_data_get_int(s, kMethodKey)),
            MirrorSettings::kSmoothingOff,
            MirrorSettings::kSmoothingLookahead
        );

        source.strengthLevel = std::clamp(
            static_cast<int>(obs_data_get_int(s, kStrengthKey)),
            kNoSmoothingLevel,
            kMaxStrengthLevel
        );
        st.strength = StrengthOfLevel(source.strengthLevel);

        st.zoom = std::clamp(float(obs_data_get_double(s, "zoom")), 1.0f, 3.0f);
        st.drawnPose = Stabilizes(st) ? kDrawnPose : 0;  // off: no slot check, no thumbnails
        source.debug = obs_data_get_bool(s, "debug");
        return source;
    }

    void SetDefaults(obs_data_t* s) {
        obs_data_set_default_int(s, kMethodKey, kDefaultMethod);
        obs_data_set_default_int(s, kStrengthKey, kDefaultStrengthLevel);
        obs_data_set_default_double(s, "zoom", kDefaultZoom);
        obs_data_set_default_bool(s, "auto_record", false);
        obs_data_set_default_bool(s, "debug", false);
    }

    bool MethodOn(obs_data_t* settings) {
        return !settings || obs_data_get_int(settings, kMethodKey) != MirrorSettings::kSmoothingOff;
    }

}
