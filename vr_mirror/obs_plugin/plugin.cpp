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
#include <string>

#include "audio_sync.h"
#include "common/settings.h"
#include "mirror_view.h"
#include "race_auto_record/auto_record.h"
#include "recording_notes.h"
#include "shared/crash_dump.h"
#include "shared/guarded.h"
#include "shared/log.h"
#include "shared/telemetry/telemetry.h"
#include "simoptix_version.h"  // written into obj\ by shared\build\version.bat
#include "video_memory.h"
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
        obs_source_t* source = nullptr;  // OBS's own (not held), for its settings in the properties
        std::unique_ptr<MirrorView> view;
        bool autoRecord = false;  // "Record races automatically" (race_auto_record\auto_record.h); UI thread
    };

    // The source's core settings are the smoothing and the zoom; the drawn pose method is fixed here.
    // 1920x1080 a little above native at Render Quality 1.0 (README.md, "Recording at the source's native
    // resolution")
    constexpr float kDefaultZoom = 1.25f;
    constexpr int kDrawnPose = 2;  // the slot check plus the cockpit correction below 90 fps

    // Race auto-record is on while at least one VR Mirror source has it ticked, and logs while one of
    // those has Debug on. UI thread.
    void FollowAutoRecord(Source* s, bool on) {
        if (on == s->autoRecord) {
            return;
        }
        s->autoRecord = on;
        if (on) {
            AutoRecord::Enable(s->view->DebugFlag());
        } else {
            AutoRecord::Disable(s->view->DebugFlag());
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

    // The video memory line for the settings in the dialog (`settings`), or for none when OBS asks for
    // the properties without a source (`s` null).
    std::string VideoMemoryOf(Source* s, obs_data_t* settings) {
        if (!s || !settings) {
            return kVideoMemoryNotMeasured;
        }

        bool debug = false;
        return s->view->VideoMemory(MirrorSettingsOf(settings, &debug));
    }

    // Smoothing or Zoom changed in the dialog: the line follows at once.
    bool RefreshVideoMemory(void* data, obs_properties_t* props, obs_property_t*, obs_data_t* settings) {
        return Guarded("video memory", [&] {
            const std::string text = VideoMemoryOf(static_cast<Source*>(data), settings);
            obs_property_set_description(obs_properties_get(props, "video_memory"), text.c_str());
            return true;
        });
    }

    obs_properties_t* MakeProperties(Source* s) {
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
        obs_property_list_add_int(
            smoothing,
            "Instant (no delay, low VRAM usage)",
            MirrorSettings::kSmoothingInstant
        );
        obs_property_list_add_int(
            smoothing,
            "Look-ahead (0.5s delay, higher VRAM usage)",
            MirrorSettings::kSmoothingLookahead
        );

        obs_property_set_long_description(
            smoothing,
            "Instant: no delay, for live streaming.\n"
            "Look-ahead: half a second behind the game, for recording."
        );

        // What the source's store of copied frames takes in video memory with these settings (the
        // line is the property's description; it has no value of its own).
        obs_data_t* settings = s ? obs_source_get_settings(s->source) : nullptr;
        const std::string memory = VideoMemoryOf(s, settings);
        obs_data_release(settings);
        obs_property_t* line = obs_properties_add_text(p, "video_memory", memory.c_str(), OBS_TEXT_INFO);
        obs_property_text_set_info_type(line, OBS_TEXT_INFO_WARNING);
        obs_property_set_modified_callback2(smoothing, RefreshVideoMemory, s);

        obs_property_t* zoom = obs_properties_add_float_slider(p, "zoom", "Zoom", 1.0, 3.0, 0.01);
        obs_property_set_modified_callback2(zoom, RefreshVideoMemory, s);
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

    obs_properties_t* GetProperties(void* data) {
        return Guarded("properties", [&] { return MakeProperties(static_cast<Source*>(data)); });
    }

    // A source on frame source `Frames` (PimaxSource, OpenXrSource).
    template <class Frames>
    void* Create(obs_data_t* settings, obs_source_t* source) {
        return Guarded("create", [&]() -> void* {
            SetCoreLog(LogToObs);
            auto s = std::make_unique<Source>();
            s->source = source;
            UpgradeSavedSmoothing(settings);
            bool debug = false;
            const MirrorSettings st = MirrorSettingsOf(settings, &debug);
            s->view = std::make_unique<MirrorView>(std::make_unique<Frames>(), st, debug);
            const ScopedLogGate gate(s->view->DebugFlag());
            FollowAutoRecord(s.get(), obs_data_get_bool(settings, "auto_record"));
            return s.release();  // OBS keeps the pointer as a void* until Destroy gives it back
        });
    }

    void Destroy(void* data) {
        Guarded("destroy", [&] {
            std::unique_ptr<Source> s(static_cast<Source*>(data));  // the one place that frees a Source
            const ScopedLogGate gate(s->view->DebugFlag());         // ends before the source is freed
            FollowAutoRecord(s.get(), false);
        });
    }

    // Each callback for one source logs as that source's Debug setting says (shared\log.h): OBS's threads
    // work for every source in turn.
    void Update(void* data, obs_data_t* settings) {
        Guarded("update", [&] {
            Source* s = static_cast<Source*>(data);
            const ScopedLogGate gate(s->view->DebugFlag());
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
        Guarded("tick", [&] {
            const ScopedLogGate gate(ViewOf(data)->DebugFlag());
            ViewOf(data)->Tick();
        });
    }

    void VideoRender(void* data, gs_effect_t*) {
        Guarded("render", [&] {
            const ScopedLogGate gate(ViewOf(data)->DebugFlag());
            ViewOf(data)->Render();
        });
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

        AudioSync::Start(&MirrorView::AnyDebug);
        RecordingNotes::Start();
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
        RecordingNotes::Stop();

        // The telemetry reader may run in this plugin for Overlays too: stop it before the DLL goes, so
        // Overlays takes it over.
        Telemetry::Shutdown();
    });
}
