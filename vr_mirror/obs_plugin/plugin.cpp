// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// SimOptix: VR Mirror, the OBS plugin: its two sources, their settings and properties, and the
// module's load and unload. SimOptix: Pimax Mirror stabilizes the Pimax client mirror (PimaxSource:
// a half-size copy of the game's left eye, from Pimax's runtime); SimOptix: OpenXR Mirror the game's
// own full-size left eye, from the OpenXR layer (OpenXrSource: it needs the layer installed, not Pimax
// Play). Both have the same settings and are drawn the same way, on OBS's own frame clock, by
// MirrorView (mirror_view.h).
//
// Each source also holds race auto-record's switch ("Record races automatically",
// race_auto_record\auto_record.h), which works whether the source is shown or not.

#include <obs-module.h>

#include <algorithm>
#include <memory>

#include "audio_sync.h"
#include "common/settings.h"
#include "mirror_view.h"
#include "race_auto_record/auto_record.h"
#include "shared/crash_dump.h"
#include "shared/guarded.h"
#include "shared/log.h"
#include "shared/telemetry/telemetry.h"
#include "simoptix_version.h"  // written into obj\ by shared\build\version.bat
#include "vr_mirror/frame_sources/openxr/openxr_source.h"
#include "vr_mirror/frame_sources/pimax/pimax_source.h"

OBS_DECLARE_MODULE()

MODULE_EXPORT const char* obs_module_name(void) {
    return "SimOptix: VR Mirror";
}

MODULE_EXPORT const char* obs_module_description(void) {
    return "SimOptix: VR Mirror: the stabilized headset view (from Pimax's mirror or from the game "
           "through the OpenXR layer), drawn on OBS's own frame clock, and race auto-record.";
}

namespace {

    void LogToObs(const char* message) {
        blog(LOG_INFO, "[simoptix_vr_mirror] %s", message);
    }

    // One source as OBS holds it: its view, and its race auto-record switch.
    struct Source {
        std::unique_ptr<MirrorView> view;
        bool autoRecord = false;  // "Record races automatically" (race_auto_record\auto_record.h); UI thread
    };

    // The source's core settings are the smoothing and the zoom; the drawn pose method is fixed here.
    // 1920x1080 a little above native at Render Quality 1.0 (README.md, "Recording at the source's native
    // resolution")
    constexpr float kDefaultZoom = 1.25f;
    constexpr int kDrawnPose = 2;  // the slot check plus the cockpit correction below 90 fps

    // Race auto-record is on while at least one VR Mirror source has it ticked. UI thread.
    void FollowAutoRecord(Source* s, bool on) {
        if (on == s->autoRecord) {
            return;
        }
        s->autoRecord = on;
        if (on) {
            AutoRecord::Enable();
        } else {
            AutoRecord::Disable();
        }
    }

    // A scene saved with the dropped Smoothest (3) shows Look-ahead in the list, not an empty choice.
    void UpgradeSavedSmoothing(obs_data_t* s) {
        if (obs_data_get_int(s, "smoothing") > MirrorSettings::kSmoothingLookahead) {
            obs_data_set_int(s, "smoothing", MirrorSettings::kSmoothingLookahead);
        }
    }

    // "smoothing": MirrorSettings::kSmoothing*. "zoom": the crop factor (1 to 3). "debug": the
    // frame-number stamp and the frame trace together.
    MirrorSettings MirrorSettingsOf(obs_data_t* s, bool* debug) {
        MirrorSettings st;
        st.smoothing = std::clamp(
            static_cast<int>(obs_data_get_int(s, "smoothing")),
            MirrorSettings::kSmoothingOff,
            MirrorSettings::kSmoothingLookahead
        );
        st.zoom = std::clamp(float(obs_data_get_double(s, "zoom")), 1.0f, 3.0f);
        st.drawnPose = Stabilizes(st) ? kDrawnPose : 0;  // off: no slot check, no thumbnails
        *debug = obs_data_get_bool(s, "debug");
        return st;
    }

    const char* GetPimaxName(void*) {
        return "SimOptix: Pimax Mirror";
    }
    const char* GetOpenXrName(void*) {
        return "SimOptix: OpenXR Mirror";
    }

    void GetDefaults(obs_data_t* s) {
        Guarded("defaults", [&] {
            obs_data_set_default_int(s, "smoothing", MirrorSettings::kSmoothingLookahead);
            obs_data_set_default_double(s, "zoom", kDefaultZoom);
            obs_data_set_default_bool(s, "auto_record", false);
            obs_data_set_default_bool(s, "debug", false);
        });
    }

    obs_properties_t* MakeProperties() {
        obs_properties_t* p = obs_properties_create();

        // Look-ahead (vr_mirror\core\pipeline\4_smoothing\): the view runs half a second behind the game,
        // and the smoothing uses the head's movement in that time.
        obs_property_t* smoothing = obs_properties_add_list(
            p,
            "smoothing",
            "Smoothing",
            OBS_COMBO_TYPE_LIST,
            OBS_COMBO_FORMAT_INT
        );

        obs_property_list_add_int(smoothing, "Off", MirrorSettings::kSmoothingOff);
        obs_property_list_add_int(smoothing, "Instant (no delay)", MirrorSettings::kSmoothingInstant);
        obs_property_list_add_int(
            smoothing,
            "Look-ahead (0.5 s behind)",
            MirrorSettings::kSmoothingLookahead
        );

        obs_property_set_long_description(
            smoothing,
            "Instant: no delay, for live streaming.\n"
            "Look-ahead: half a second behind the game, for recording.\n"
            "The G-meter and the sound follow the same delay."
        );

        obs_properties_add_float_slider(p, "zoom", "Zoom", 1.0, 3.0, 0.01);
        obs_property_t* record = obs_properties_add_bool(p, "auto_record", "Record races automatically");

        // The hint, one short line per point (OBS shows it as a plain-text tooltip, which doesn't wrap).
        obs_property_set_long_description(
            record,
            "Races only, not practice or qualifying.\n"
            "Starts at the start lights, stops 5 s after the finish.\n"
            "Recordings you start yourself are left alone."
        );

        obs_properties_add_bool(p, "debug", "Debug");
        return p;
    }

    obs_properties_t* GetProperties(void*) {
        return Guarded("properties", [] { return MakeProperties(); });
    }

    // A source on frame source `Frames` (PimaxSource, OpenXrSource).
    template <class Frames>
    void* Create(obs_data_t* settings, obs_source_t*) {
        return Guarded("create", [&]() -> void* {
            SetCoreLog(LogToObs);
            auto s = std::make_unique<Source>();
            UpgradeSavedSmoothing(settings);
            bool debug = false;
            const MirrorSettings st = MirrorSettingsOf(settings, &debug);
            s->view = std::make_unique<MirrorView>(std::make_unique<Frames>(), st, debug);
            FollowAutoRecord(s.get(), obs_data_get_bool(settings, "auto_record"));
            return s.release();  // OBS keeps the pointer as a void* until Destroy gives it back
        });
    }

    void Destroy(void* data) {
        Guarded("destroy", [&] {
            std::unique_ptr<Source> s(static_cast<Source*>(data));  // the one place that frees a Source
            FollowAutoRecord(s.get(), false);
        });
    }

    void Update(void* data, obs_data_t* settings) {
        Guarded("update", [&] {
            Source* s = static_cast<Source*>(data);
            bool debug = false;
            const MirrorSettings st = MirrorSettingsOf(settings, &debug);
            s->view->SetSettings(st, debug);
            FollowAutoRecord(s, obs_data_get_bool(settings, "auto_record"));
        });
    }

    MirrorView* ViewOf(void* data) {
        return static_cast<Source*>(data)->view.get();
    }

    uint32_t GetWidth(void* data) {
        return Guarded("width", [&] { return ViewOf(data)->Width(); });
    }

    uint32_t GetHeight(void* data) {
        return Guarded("height", [&] { return ViewOf(data)->Height(); });
    }

    void Show(void* data) {
        Guarded("show", [&] { ViewOf(data)->Show(); });
    }

    void Hide(void* data) {
        Guarded("hide", [&] { ViewOf(data)->Hide(); });
    }

    void VideoTick(void* data, float) {
        Guarded("tick", [&] { ViewOf(data)->Tick(); });
    }

    void VideoRender(void* data, gs_effect_t*) {
        Guarded("render", [&] { ViewOf(data)->Render(); });
    }

    bool LoadModule() {
        const int dumpsDeleted = InstallCrashDump(L"vr_mirror");
        SetCoreLog(LogToObs);  // before any source: race auto-record and the telemetry log too

        if (dumpsDeleted > 0) {
            blog(LOG_INFO, "[simoptix_vr_mirror] %d older crash dump(s) deleted", dumpsDeleted);
        }

        obs_source_info info = {};
        info.type = OBS_SOURCE_TYPE_INPUT;
        info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;

        info.destroy = Destroy;
        info.update = Update;
        info.get_defaults = GetDefaults;
        info.get_properties = GetProperties;
        info.get_width = GetWidth;
        info.get_height = GetHeight;
        info.video_tick = VideoTick;
        info.show = Show;
        info.hide = Hide;
        info.video_render = VideoRender;

        info.id = "simoptix_pimax_mirror";
        info.get_name = GetPimaxName;
        info.create = Create<PimaxSource>;
        obs_register_source(&info);

        info.id = "simoptix_openxr_mirror";
        info.get_name = GetOpenXrName;
        info.create = Create<OpenXrSource>;
        obs_register_source(&info);

        AudioSync::Start();
        blog(LOG_INFO, "[simoptix_vr_mirror] loaded, version %s", SIMOPTIX_VERSION);
        return true;
    }

}

bool obs_module_load(void) {
    return Guarded("load", [] { return LoadModule(); });
}

void obs_module_unload(void) {
    Guarded("unload", [] {
        AudioSync::Stop();

        // The telemetry reader may run in this plugin for Overlays too: stop it before the DLL goes, so
        // Overlays takes it over.
        Telemetry::Shutdown();
    });
}
