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

#include "audio_sync.h"
#include "common/settings.h"
#include "mirror_view.h"
#include "race_auto_record/auto_record.h"
#include "shared/crash_dump.h"
#include "shared/guarded.h"
#include "shared/local_files.h"
#include "shared/log.h"
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

    // What the demo's buttons and status line show, as one number: a change refreshes the properties.
    int DemoKey(const MirrorView& view) {
        return static_cast<int>(view.LapState()) * 4 + (view.RecordingLap() ? 2 : 0)
               + (view.DemoOn() ? 1 : 0);
    }

    // UI thread: shows the demo's buttons and status as they are now. `data`: a weak reference, released
    // here.
    void RefreshProperties(void* data) {
        obs_weak_source_t* weak = static_cast<obs_weak_source_t*>(data);

        if (obs_source_t* source = obs_weak_source_get_source(weak)) {
            obs_source_update_properties(source);
            obs_source_release(source);
        }

        obs_weak_source_release(weak);
    }

    // The demo's status line, while a lap is asked for or just after: where the recording stands; null
    // otherwise.
    const char* LapStatus(const MirrorView& view) {
        switch (view.LapState()) {
            case LapRecorder::State::kWaiting:
                return "Drive to the start/finish line: leave the pits, and the lap records from the next "
                       "crossing.";
            case LapRecorder::State::kRecording:
                return "Recording the lap: drive on to the start/finish line.";
            case LapRecorder::State::kSaving:
                return "Saving the lap.";
            case LapRecorder::State::kSaved:
                return "Lap saved: \"Play recorded lap\" plays it.";
            case LapRecorder::State::kFailed:
                return "The lap wasn't recorded: the OBS log says why.";
            default:
                break;
        }

        if (view.RecordingLap()) {
            return "The lap records once the game's frames come (the source shown, the recorded lap "
                   "stopped).";
        }

        return nullptr;  // no recording asked for: the view's empty state says whether a lap is recorded
    }

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

    const char* GetPimaxName(void*) {
        return "SimOptix: Pimax Mirror";
    }
    const char* GetOpenXrName(void*) {
        return "SimOptix: OpenXR Mirror";
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

    // UI thread: records the next lap, or gives up the one asked for. The demo stops: a lap is the game's.
    // Recording over a recorded lap asks first.
    bool ToggleRecordLap(obs_properties_t*, obs_property_t*, void* data) {
        return Guarded("record lap button", [&]() -> bool {
            MirrorView& view = *static_cast<Source*>(data)->view;
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

            if (on) {
                view.SetDemo(false);
            }

            return true;  // the properties again, with the buttons as they are now
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
            if (!Confirm(L"Delete recorded lap", L"Delete the recorded lap? This can't be undone.")) {
                return false;
            }

            const int deleted = PruneLocalFiles(
                LocalFileKind::kRecordedLap,
                LocalFilesKept(LocalFileKind::kRecordedLap)
            );
            blog(LOG_INFO, "[simoptix_vr_mirror] recorded lap deleted (%d)", deleted);
            static_cast<Source*>(data)->view->FindLap();
            return true;  // the properties again, with the buttons as they are now
        });
    }

    // UI thread: plays the newest recorded lap, or goes back to the game.
    bool ToggleDemo(obs_properties_t*, obs_property_t*, void* data) {
        return Guarded("demo button", [&]() -> bool {
            MirrorView& view = *static_cast<Source*>(data)->view;
            const bool on = !view.DemoOn();
            view.SetDemo(on);

            if (on) {
                view.SetRecordLap(false);
            }

            return true;  // the properties again, with the buttons as they are now
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

    // The demo's buttons, in the settings' column under a rule, and the recording's status line under them
    // while there is one, for source `s`. No group: OBS lays a group out on a grid of its own, out of line
    // with the settings, and puts each button on a row of its own anyway.
    void AddDemoProperties(obs_properties_t* p, Source* s) {
        std::wstring stem;
        const bool lapRecorded = NewestRecordedLap(&stem);
        const bool recording = s->view->RecordingLap();
        const bool demo = s->view->DemoOn();

        // OBS has no separator property; its text lines are rich text, stretched across the settings' column.
        obs_properties_add_text(p, "demo_rule", "<hr>", OBS_TEXT_INFO);

        obs_property_t* play = obs_properties_add_button2(
            p,
            "demo",
            demo ? "Stop playing" : "Play recorded lap",
            ToggleDemo,
            s
        );

        obs_property_set_long_description(
            play,
            lapRecorded || demo ? "Plays the recorded lap in a loop, in place of the game.\n"
                                  "The settings above apply as it plays."
                                : "No lap recorded yet: record one with \"Record a lap\"."
        );

        obs_property_set_enabled(play, demo || lapRecorded);

        const char* recordLabel = lapRecorded ? "Record a new lap" : "Record a lap";
        obs_property_t* record = obs_properties_add_button2(
            p,
            "record_lap",
            recording ? "Cancel lap recording" : recordLabel,
            ToggleRecordLap,
            s
        );

        obs_property_set_long_description(
            record,
            "Drive one full lap in LMU, in practice.\n"
            "It records from a crossing of the start/finish line to the next.\n"
            "Entering the pits or pausing the game starts the lap over at the next start/finish line."
        );

        obs_property_t* open = obs_properties_add_button2(
            p,
            "open_lap_folder",
            "Open recording location",
            OpenLapFolder,
            s
        );
        obs_property_set_long_description(open, "Opens the folder the recorded lap is kept in.");

        obs_property_t*
            remove = obs_properties_add_button2(p, "delete_lap", "Delete recorded lap", DeleteLap, s);
        obs_property_set_long_description(
            remove,
            demo || recording ? "Stop playing the lap, or cancel the recording, first."
                              : "Deletes the recorded lap. Asks first."
        );
        obs_property_set_enabled(remove, lapRecorded && !demo && !recording);

        if (const char* status = LapStatus(*s->view)) {
            obs_property_text_set_info_word_wrap(
                obs_properties_add_text(p, "lap_status", status, OBS_TEXT_INFO),
                true
            );
        }
    }

    // The view draws its empty state only while a properties dialog shows it, never only in the recording. A
    // dialog holds the list it asked for, and OBS destroys the list when the dialog closes, or when it asks
    // for a new one: the list counts the dialog in while it lives (OBS calls `destroy` with the parameter).
    void CountDialog(obs_properties_t* p, const MirrorView& view) {
        using Viewers = std::shared_ptr<std::atomic<int>>;
        Viewers* viewers = new Viewers(view.EmptyStateViewers());  // owned by the list from here
        ++**viewers;

        obs_properties_set_param(p, viewers, [](void* param) {
            const std::unique_ptr<Viewers> owned(static_cast<Viewers*>(param));
            --**owned;
        });
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
            "How strongly Instant and Look-ahead smooth, from 0 to 10. 5 is the tuned default.\n"
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
            CountDialog(p, *s->view);
            s->view->DialogOpened();
        }

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
            s->weak = obs_source_get_weak_source(source);
            UpgradeSavedSettings(settings);
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
            obs_weak_source_release(s->weak);
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
        Guarded("tick", [&] {
            Source* s = static_cast<Source*>(data);
            s->view->Tick();

            // The lap recorder moves on by itself: its buttons and status follow in the properties, if open.
            const int key = DemoKey(*s->view);

            if (key != s->shownDemo && s->weak) {
                s->shownDemo = key;
                obs_weak_source_addref(s->weak);
                obs_queue_task(OBS_TASK_UI, RefreshProperties, s->weak, false);
            }
        });
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
