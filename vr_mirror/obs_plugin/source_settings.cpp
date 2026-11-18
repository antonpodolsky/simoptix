// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "source_settings.h"

#include <algorithm>
#include <cmath>

namespace source_settings {

    namespace {

        // The strength at `level`: below the default, even steps on a log scale from 1 down to kMinStrength;
        // above it, from 1 up to kMaxStrength.
        float StrengthOfLevel(int level) {
            const int steps = level - kDefaultStrengthLevel;

            if (steps < 0) {
                const float stepsDown = static_cast<float>(kDefaultStrengthLevel - kMinStrengthLevel);
                return std::pow(kMinStrength, static_cast<float>(-steps) / stepsDown);
            }

            const float stepsUp = static_cast<float>(kMaxStrengthLevel - kDefaultStrengthLevel);
            return std::pow(kMaxStrength, static_cast<float>(steps) / stepsUp);
        }

        // The level nearest `strength` on StrengthOfLevel's scale.
        int LevelOfStrength(float strength) {
            const float clamped = std::clamp(strength, kMinStrength, kMaxStrength);
            float steps = 0;

            if (clamped < 1) {
                const float stepsDown = static_cast<float>(kDefaultStrengthLevel - kMinStrengthLevel);
                steps = -std::log(clamped) / std::log(kMinStrength) * stepsDown;
            } else {
                const float stepsUp = static_cast<float>(kMaxStrengthLevel - kDefaultStrengthLevel);
                steps = std::log(clamped) / std::log(kMaxStrength) * stepsUp;
            }

            return kDefaultStrengthLevel + static_cast<int>(std::lround(steps));
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

        for (const char* strengthKey : {"smoothing_strength", "stabilization_strength"}) {
            if (obs_data_has_user_value(s, strengthKey)) {
                const float strength = static_cast<float>(obs_data_get_double(s, strengthKey));
                obs_data_set_int(s, kStrengthKey, LevelOfStrength(strength));
                obs_data_erase(s, strengthKey);
            }
        }
    }

    MirrorSettings MirrorSettingsOf(obs_data_t* s, bool* debug) {
        MirrorSettings st;
        st.smoothing = std::clamp(
            static_cast<int>(obs_data_get_int(s, kMethodKey)),
            MirrorSettings::kSmoothingOff,
            MirrorSettings::kSmoothingLookahead
        );

        const int level = std::clamp(
            static_cast<int>(obs_data_get_int(s, kStrengthKey)),
            kMinStrengthLevel,
            kMaxStrengthLevel
        );
        st.strength = StrengthOfLevel(level);

        st.zoom = std::clamp(float(obs_data_get_double(s, "zoom")), 1.0f, 3.0f);
        st.drawnPose = Stabilizes(st) ? kDrawnPose : 0;  // off: no slot check, no thumbnails
        *debug = obs_data_get_bool(s, "debug");
        return st;
    }

    void SetDefaults(obs_data_t* s) {
        obs_data_set_default_int(s, kMethodKey, MirrorSettings::kSmoothingLookahead);
        obs_data_set_default_int(s, kStrengthKey, kDefaultStrengthLevel);
        obs_data_set_default_double(s, "zoom", kDefaultZoom);
        obs_data_set_default_bool(s, "auto_record", false);
        obs_data_set_default_bool(s, "debug", false);
    }

    bool MethodOn(obs_data_t* settings) {
        return !settings || obs_data_get_int(settings, kMethodKey) != MirrorSettings::kSmoothingOff;
    }

}
