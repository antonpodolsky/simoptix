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
// race_auto_record\auto_record.h), which works whether the source is shown or not, and the demo's two
// buttons: Record a lap (the next full lap the game shows, lap_recorder\lap_recorder.h), Play recorded lap
// (the lap in place of the game, vr_mirror\frame_sources\demo\) and Delete recorded lap. None is saved.

#include <windows.h>
#include <obs-module.h>
#include <shellapi.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "audio_sync.h"
#include "common/settings.h"
#include "mirror_view.h"
#include "race_auto_record/auto_record.h"
#include "recording_notes.h"
#include "shared/crash_dump.h"
#include "shared/guarded.h"
#include "shared/local_files.h"
#include "shared/log.h"
#include "shared/source_icon.h"
#include "shared/telemetry/telemetry.h"
#include "simoptix_version.h"  // written into obj\ by shared\build\version.bat
#include "source_settings.h"
#include "video_memory.h"
#include "vr_mirror/frame_sources/demo/recorded_lap.h"
#include "vr_mirror/frame_sources/openxr/openxr_source.h"
#include "vr_mirror/frame_sources/pimax/pimax_source.h"

OBS_DECLARE_MODULE()

using namespace source_settings;

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

    // One source as OBS holds it: its view, its race auto-record switch, and what its properties last showed
    // of the demo.
    struct Source {
        obs_source_t* source = nullptr;  // OBS's own (not held), for its settings in the properties
        std::unique_ptr<MirrorView> view;
        bool autoRecord = false;  // "Record races automatically" (race_auto_record\auto_record.h); UI thread
        obs_weak_source_t* weak = nullptr;  // to refresh its properties from the UI thread
        int shownDemo = -1;                 // graphics thread: DemoKey as the properties were last refreshed
    };

    // What the demo's buttons show, as one number: a change refreshes the properties, once. The recorder's
    // own states (waiting, recording, saving) change nothing there, so they don't refresh it.
    int DemoKey(const MirrorView& view) {
        return (view.LapRecorded() ? 8 : 0) + (view.HeadsetFound() ? 4 : 0) + (view.RecordingLap() ? 2 : 0)
               + (view.DemoOn() ? 1 : 0);
    }

    // UI thread: shows the demo's buttons as they are now. `data`: a weak reference, released
    // here.
    void RefreshProperties(void* data) {
        obs_weak_source_t* weak = static_cast<obs_weak_source_t*>(data);

        if (obs_source_t* source = obs_weak_source_get_source(weak)) {
            obs_source_update_properties(source);
            obs_source_release(source);
        }

        obs_weak_source_release(weak);
    }

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

    const char* GetPimaxName(void*) {
        return "SimOptix: Pimax Mirror";
    }
    const char* GetOpenXrName(void*) {
        return "SimOptix: OpenXR Mirror";
    }

    const char* GetDarkIcon(void*) {
        return Guarded("dark icon", [] { return FindSourceIcon("vr_headset_dark.svg"); });
    }

    const char* GetLightIcon(void*) {
        return Guarded("light icon", [] { return FindSourceIcon("vr_headset_light.svg"); });
    }

    // What the empty state asks for when the source on frame source `Frames` can't read the headset
    // (MirrorView's `headsetSteps`).
    template <class Frames>
    std::vector<std::wstring> HeadsetSteps();

    // Pimax Mirror reads the headset through Pimax Play.
    template <>
    std::vector<std::wstring> HeadsetSteps<PimaxSource>() {
        return {L"Turn the headset on", L"Start Pimax Play"};
    }

    // OpenXR Mirror reads it through whichever VR software is the active OpenXR runtime. Meta's runs the
    // headset test only while the headset is worn (findings\obs_plugin.md, "The empty state's facts").
    template <>
    std::vector<std::wstring> HeadsetSteps<OpenXrSource>() {
        return {L"Turn the headset on and start its VR software", L"Put the headset on"};
    }

    void GetDefaults(obs_data_t* s) {
        Guarded("defaults", [&] { SetDefaults(s); });
    }

    // Asks `question`, Yes or No (No by default), in a box over the properties dialog: the buttons' callbacks
    // run on OBS's UI thread, whose active window it is. True for Yes.
    bool Confirm(const wchar_t* title, const wchar_t* question) {
        return MessageBoxW(GetActiveWindow(), question, title, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2)
               == IDYES;
    }

    // UI thread: records the next lap, or gives up the one asked for. Greyed out while the demo plays (a lap
    // is the game's). Recording over a recorded lap asks first.
    bool ToggleRecordLap(obs_properties_t*, obs_property_t*, void* data) {
        return Guarded("record lap button", [&]() -> bool {
            MirrorView& view = *static_cast<Source*>(data)->view;
            const ScopedLogGate gate(view.DebugFlag());
            const bool on = !view.RecordingLap();
            std::wstring stem;

            if (on && NewestRecordedLap(&stem)
                && !Confirm(
                    L"Record a new lap",
                    L"Record a new lap? Once it is saved, it replaces the existing recording."
                )) {
                return false;
            }

            view.SetRecordLap(on);
            return false;  // the source's refresh (DemoKey) redraws the buttons, once
        });
    }

    // UI thread: opens the folder the recorded lap is kept in, in File Explorer, with the lap's video
    // selected when there is one. Nothing in the properties changes.
    bool OpenLapFolder(obs_properties_t*, obs_property_t*, void*) {
        return Guarded("open lap folder button", [&]() -> bool {
            std::wstring stem, arguments;
            wchar_t file[MAX_PATH];

            if (NewestRecordedLap(&stem)) {
                arguments = L"/select,\"" + stem + L".mp4\"";
            } else if (NewLocalFilePath(LocalFileKind::kRecordedLap, file, MAX_PATH)) {  // makes the folder
                arguments = file;
                arguments = L"\"" + arguments.substr(0, arguments.find_last_of(L'\\')) + L"\"";
            }

            if (!arguments.empty()) {
                ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
            }

            return false;
        });
    }

    // UI thread: deletes the recorded lap, after asking. Greyed out while it plays or a lap records.
    bool DeleteLap(obs_properties_t*, obs_property_t*, void* data) {
        return Guarded("delete lap button", [&]() -> bool {
            MirrorView& view = *static_cast<Source*>(data)->view;
            const ScopedLogGate gate(view.DebugFlag());

            if (!Confirm(L"Delete recorded lap", L"Delete the recorded lap? This can't be undone.")) {
                return false;
            }

            const int deleted = PruneLocalFiles(
                LocalFileKind::kRecordedLap,
                LocalFilesKept(LocalFileKind::kRecordedLap)
            );
            CoreLog("recorded lap deleted (%d)", deleted);
            view.FindLap();
            return false;  // the source's refresh (DemoKey) redraws the buttons, once
        });
    }

    // UI thread: plays the newest recorded lap, or goes back to the game.
    bool ToggleDemo(obs_properties_t*, obs_property_t*, void* data) {
        return Guarded("demo button", [&]() -> bool {
            MirrorView& view = *static_cast<Source*>(data)->view;
            const ScopedLogGate gate(view.DebugFlag());
            const bool on = !view.DemoOn();
            view.SetDemo(on);

            if (on) {
                view.SetRecordLap(false);
            }

            return false;  // the source's refresh (DemoKey) redraws the buttons, once
        });
    }

    // Stabilization Method changed in the dialog: Stabilization Strength is hidden while it is Off (OBS
    // greys out only the number box of a slider, not the slider).
    bool FollowMethod(obs_properties_t* props, obs_property_t*, obs_data_t* settings) {
        return Guarded("settings in the dialog", [&] {
            obs_property_set_visible(obs_properties_get(props, kStrengthKey), MethodOn(settings));
            return true;
        });
    }

    // The demo's buttons, in the settings' column under a rule, for source `s`. Without a recorded lap only
    // Record a lap shows: there is nothing to play, open or delete. No group: OBS lays a group out on a grid
    // of its own, out of line with the settings, and puts each button on a row of its own anyway.
    void AddDemoProperties(obs_properties_t* p, Source* s) {
        std::wstring stem;
        const bool recording = s->view->RecordingLap();
        const bool demo = s->view->DemoOn();
        const bool hasLap = NewestRecordedLap(&stem) || demo;

        // OBS has no separator property; its text lines are rich text, stretched across the settings' column.
        // An <hr> takes the text's own colour, so the rule is a 1 px block in Yami's border colour
        // (--border_color, #3C404D), a step above the dialog's background, with an empty 6 px line above and
        // below it for space.
        obs_properties_add_text(
            p,
            "demo_rule",
            "<div style=\"font-size:6px;\">&nbsp;</div>"
            "<div style=\"background-color:#3c404d; font-size:1px;\">&nbsp;</div>"
            "<div style=\"font-size:6px;\">&nbsp;</div>",
            OBS_TEXT_INFO
        );

        if (hasLap) {
            obs_property_t* play = obs_properties_add_button2(
                p,
                "demo",
                demo ? "Stop playing" : "Play recorded lap",
                ToggleDemo,
                s
            );

            obs_property_set_long_description(
                play,
                "Plays the recorded lap in a loop, in place of the game.\n"
                "The settings above apply as it plays."
            );
        }

        const char* recordLabel = hasLap ? "Record a new lap" : "Record a lap";
        obs_property_t* record = obs_properties_add_button2(
            p,
            "record_lap",
            recording ? "Cancel lap recording" : recordLabel,
            ToggleRecordLap,
            s
        );

        // Without a headset found there is no game to record, and while the lap plays the game isn't shown. A
        // recording asked for still gets its Cancel.
        const bool headset = s->view->HeadsetFound();
        const bool canRecord = recording || (headset && !demo);
        obs_property_set_enabled(record, canRecord);

        const char* recordHint = "Drive one full lap in LMU, in practice.\n"
                                 "It records from a crossing of the start/finish line to the next.\n"
                                 "Entering the pits or pausing the game starts the lap over at the next "
                                 "start/finish line.";

        if (!canRecord) {
            recordHint = demo ? "Stop playing the lap first."
                              : "No headset found yet.\n"
                                "Turn the headset on and start its VR software, then open this window again.";
        }

        obs_property_set_long_description(record, recordHint);

        if (hasLap) {
            obs_property_t*
                remove = obs_properties_add_button2(p, "delete_lap", "Delete recorded lap", DeleteLap, s);
            obs_property_set_long_description(
                remove,
                demo || recording ? "Stop playing the lap, or cancel the recording, first."
                                  : "Deletes the recorded lap. Asks first."
            );
            obs_property_set_enabled(remove, !demo && !recording);

            obs_property_t* open = obs_properties_add_button2(
                p,
                "open_lap_folder",
                "Open recording location",
                OpenLapFolder,
                s
            );
            obs_property_set_long_description(open, "Opens the folder the recorded lap is kept in.");
        }
    }

    // The view draws its empty state only while a properties dialog shows it, never only in the recording. A
    // dialog holds the list it asked for, and OBS destroys the list when the dialog closes, or when it asks
    // for a new one: the list counts the dialog in while it lives (OBS calls `destroy` with the parameter).
    // True when it is the only one: a dialog opened. A refresh asks for the new list before it drops the old
    // (the properties view's ReloadProperties), so the count goes from 1 to 2 and back, and it is false.
    bool CountDialog(obs_properties_t* p, const MirrorView& view) {
        using Viewers = std::shared_ptr<std::atomic<int>>;
        Viewers* viewers = new Viewers(view.EmptyStateViewers());  // owned by the list from here
        const bool opened = ++**viewers == 1;

        obs_properties_set_param(p, viewers, [](void* param) {
            const std::unique_ptr<Viewers> owned(static_cast<Viewers*>(param));
            --**owned;
        });

        return opened;
    }

    // `s`: the source, or null for OBS's defaults (no demo buttons then).
    obs_properties_t* MakeProperties(Source* s) {
        obs_properties_t* p = obs_properties_create();

        // Look-ahead (vr_mirror\core\pipeline\4_smoothing\): the view runs half a second behind the game,
        // and the smoothing uses the head's movement in that time.
        obs_property_t* method = obs_properties_add_list(
            p,
            kMethodKey,
            "Stabilization Method",
            OBS_COMBO_TYPE_LIST,
            OBS_COMBO_FORMAT_INT
        );

        obs_property_list_add_int(method, "Off", MirrorSettings::kSmoothingOff);
        obs_property_list_add_int(
            method,
            "Instant (no delay, low VRAM usage)",
            MirrorSettings::kSmoothingInstant
        );
        obs_property_list_add_int(
            method,
            "Look-ahead (0.5s delay, higher VRAM usage)",
            MirrorSettings::kSmoothingLookahead
        );

        obs_property_set_long_description(
            method,
            "Instant: no delay, for live streaming.\n"
            "Look-ahead: half a second behind the game, for recording."
        );

        obs_data_t* settings = s ? obs_source_get_settings(s->source) : nullptr;
        const bool methodOn = MethodOn(settings);
        obs_data_release(settings);
        obs_property_set_modified_callback(method, FollowMethod);

        obs_property_t* strength = obs_properties_add_int_slider(
            p,
            kStrengthKey,
            "Stabilization Strength",
            kNoSmoothingLevel,
            kMaxStrengthLevel,
            1
        );

        obs_property_set_visible(strength, methodOn);

        obs_property_set_long_description(
            strength,
            "How strongly Instant and Look-ahead smooth, from 0 to 10. 3 by default.\n"
            "5: each method's own tuning.\n"
            "Higher: calmer, the view follows the head less closely.\n"
            "Lower: the view follows the head more closely.\n"
            "0: no smoothing. The horizon is still kept level."
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

        if (s) {
            AddDemoProperties(p, s);

            // Only an opening reads the headset: a refresh restarted the source, which refreshed it again.
            if (CountDialog(p, *s->view)) {
                s->view->DialogOpened();
            }
        }

        return p;
    }

    obs_properties_t* GetProperties(void* data) {
        return Guarded("properties", [&] {
            Source* s = static_cast<Source*>(data);
            const ScopedLogGate gate(s->view->DebugFlag());
            return MakeProperties(s);
        });
    }

    // A source on frame source `Frames` (PimaxSource, OpenXrSource).
    template <class Frames>
    void* Create(obs_data_t* settings, obs_source_t* source) {
        return Guarded("create", [&]() -> void* {
            SetCoreLog(LogToObs);
            auto s = std::make_unique<Source>();
            s->source = source;
            s->weak = obs_source_get_weak_source(source);
            UpgradeSavedSettings(settings);
            bool debug = false;
            const MirrorSettings st = MirrorSettingsOf(settings, &debug);
            s->view = std::make_unique<MirrorView>(
                std::make_unique<Frames>(),
                HeadsetSteps<Frames>(),
                st,
                debug
            );
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
            obs_weak_source_release(s->weak);
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
            Source* s = static_cast<Source*>(data);
            const ScopedLogGate gate(s->view->DebugFlag());
            s->view->Tick();

            // The lap recorder moves on by itself: its buttons follow in the properties, if open.
            const int key = DemoKey(*s->view);

            if (key != s->shownDemo && s->weak) {
                s->shownDemo = key;
                obs_weak_source_addref(s->weak);
                obs_queue_task(OBS_TASK_UI, RefreshProperties, s->weak, false);
            }
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
        info.icon_type = OBS_ICON_TYPE_CUSTOM;  // both sources show the same view, so one icon
        info.get_dark_icon = GetDarkIcon;
        info.get_light_icon = GetLightIcon;

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
