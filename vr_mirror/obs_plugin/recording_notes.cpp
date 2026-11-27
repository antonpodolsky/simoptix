// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "recording_notes.h"

#include <windows.h>
#include <obs-module.h>
#include <util/platform.h>

#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "shared/guarded.h"
#include "shared/log.h"
#include "simoptix_version.h"  // written into obj\ by shared\build\version.bat

namespace {

    // OBS's frontend API (obs-frontend-api.h in OBS 32): its events, by their place in
    // `enum obs_frontend_event`, and the calls used here.
    constexpr int kRecordingStarted = 5;  // OBS_FRONTEND_EVENT_RECORDING_STARTED
    constexpr int kRecordingStopped = 7;  // OBS_FRONTEND_EVENT_RECORDING_STOPPED
    constexpr int kExit = 17;             // OBS_FRONTEND_EVENT_EXIT
    using EventCallback = void (*)(int event, void* data);
    using EventCallbackFn = void (*)(EventCallback callback, void* data);
    using RecordingOutputFn = obs_output_t* (*)(void);

    struct Source {
        std::string name;
        std::map<std::string, std::string> notes;  // by what they are ("settings", "eye", ...)
        bool debug = false;                        // its Debug setting is on: only then is it noted
    };

    // UI thread only.
    struct State {
        EventCallbackFn removeCallback = nullptr;
        RecordingOutputFn recordingOutput = nullptr;
        std::map<const void*, Source> sources;
        std::wstring file;     // the notes file of the recording running; empty: none
        uint64_t startNs = 0;  // when that recording started (os_gettime_ns)
    };

    // Never destroyed, like race auto-record's: a change queued to the UI thread may still run while the
    // DLL unloads.
    State& TheState() {
        static State* state = new State();
        return *state;
    }

    // One change from Note, SetDebug or Forget, queued to the UI thread.
    struct Change {
        enum class Kind { kNote, kDebug, kForget };
        Kind kind = Kind::kNote;
        const void* source = nullptr;
        std::string name, what, text;
        bool debug = false;  // kDebug's value
        uint64_t ns = 0;     // when it was noted (os_gettime_ns)
    };

    // "m:ss.sss <name>: <what>: <text>", at `ns` into the recording.
    std::string Line(uint64_t ns, const std::string& name, const std::string& what, const std::string& text) {
        const double s = static_cast<double>(ns) / 1e9;
        char time[32];
        snprintf(time, sizeof(time), "%d:%06.3f", static_cast<int>(s / 60), std::fmod(s, 60.0));
        return std::string(time) + " " + name + ": " + what + ": " + text + "\n";
    }

    // Writes `text` to the notes file: a new file (`mode` L"wb") or at its end (L"ab").
    void Write(const std::wstring& file, const wchar_t* mode, const std::string& text) {
        const std::unique_ptr<FILE, decltype(&fclose)> f(_wfopen(file.c_str(), mode), &fclose);

        if (!f) {
            CoreLogError("recording notes: could not write the file beside the recording");
            return;
        }

        fputs(text.c_str(), f.get());
    }

    // The notes file for the recording OBS just started: its file name with `.simoptix.txt` in place of
    // its extension. Empty if OBS doesn't say where it records.
    std::wstring NotesFile(const State& st) {
        obs_output_t* output = st.recordingOutput();

        if (!output) {
            return {};
        }

        obs_data_t* settings = obs_output_get_settings(output);
        const std::string path = settings ? obs_data_get_string(settings, "path") : "";
        obs_data_release(settings);
        obs_output_release(output);

        if (path.empty()) {
            return {};
        }

        wchar_t wide[MAX_PATH * 2];

        if (!MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide, MAX_PATH * 2)) {
            return {};
        }

        std::wstring file = wide;
        const size_t dot = file.find_last_of(L'.');
        const size_t slash = file.find_last_of(L"\\/");

        if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)) {
            file.resize(dot);
        }

        return file + L".simoptix.txt";
    }

    bool AnyDebug(const State& st) {
        for (const auto& source : st.sources) {
            if (source.second.debug) {
                return true;
            }
        }

        return false;
    }

    // A recording started: its notes file, if a source has Debug on.
    void StartNotes(State& st) {
        st.file.clear();

        if (!AnyDebug(st)) {
            return;
        }

        st.file = NotesFile(st);

        if (st.file.empty()) {
            CoreLogError(
                "recording notes: OBS didn't say where it records, so the recording gets no notes file"
            );
            return;
        }

        st.startNs = os_gettime_ns();
        std::string
            text = "SimOptix VR Mirror " SIMOPTIX_VERSION
                   "\nVideo times from the start of the recording. Each line is what a VR Mirror source "
                   "runs with from then on.\n";

        for (const auto& source : st.sources) {
            if (!source.second.debug) {
                continue;
            }

            for (const auto& note : source.second.notes) {
                text += Line(0, source.second.name, note.first, note.second);
            }
        }

        Write(st.file, L"wb", text);
    }

    void OnEvent(int event, void*) {
        Guarded("recording event", [&] {
            State& st = TheState();

            if (event == kRecordingStarted) {
                StartNotes(st);
            } else if (event == kRecordingStopped || event == kExit) {
                st.file.clear();
            }
        });
    }

    void ApplyChange(void* data) {
        Guarded("recording note", [&] {
            const std::unique_ptr<Change> change(static_cast<Change*>(data));  // the one place that frees it
            State& st = TheState();

            if (change->kind == Change::Kind::kForget) {
                st.sources.erase(change->source);
                return;
            }

            Source& source = st.sources[change->source];
            source.name = change->name;

            if (change->kind == Change::Kind::kDebug) {
                source.debug = change->debug;
                return;
            }

            source.notes[change->what] = change->text;

            if (!st.file.empty() && source.debug) {
                const uint64_t ns = change->ns > st.startNs ? change->ns - st.startNs : 0;
                Write(st.file, L"ab", Line(ns, change->name, change->what, change->text));
            }
        });
    }

    void Queue(std::unique_ptr<Change> change) {
        change->ns = os_gettime_ns();
        obs_queue_task(OBS_TASK_UI, ApplyChange, change.release(), false);  // ApplyChange takes it back
    }

}

void RecordingNotes::Start() {
    State& st = TheState();
    const HMODULE frontend = GetModuleHandleW(L"obs-frontend-api.dll");
    EventCallbackFn add = nullptr;

    if (frontend) {
        add = reinterpret_cast<EventCallbackFn>(GetProcAddress(frontend, "obs_frontend_add_event_callback"));
        st.removeCallback = reinterpret_cast<EventCallbackFn>(
            GetProcAddress(frontend, "obs_frontend_remove_event_callback")
        );
        st.recordingOutput = reinterpret_cast<RecordingOutputFn>(
            GetProcAddress(frontend, "obs_frontend_get_recording_output")
        );
    }

    if (!add || !st.removeCallback || !st.recordingOutput) {
        st.removeCallback = nullptr;
        CoreLogError("recording notes: OBS's frontend API isn't there, so recordings get no notes file");
        return;
    }

    add(OnEvent, nullptr);
}

void RecordingNotes::Stop() {
    State& st = TheState();

    if (st.removeCallback) {
        st.removeCallback(OnEvent, nullptr);
        st.removeCallback = nullptr;
    }
}

void RecordingNotes::Note(const void* source, const char* name, const char* what, const char* text) {
    auto change = std::make_unique<Change>();
    change->source = source;
    change->name = name;
    change->what = what;
    change->text = text;
    Queue(std::move(change));
}

void RecordingNotes::SetDebug(const void* source, const char* name, bool on) {
    auto change = std::make_unique<Change>();
    change->kind = Change::Kind::kDebug;
    change->source = source;
    change->name = name;
    change->debug = on;
    Queue(std::move(change));
}

void RecordingNotes::Forget(const void* source) {
    auto change = std::make_unique<Change>();
    change->kind = Change::Kind::kForget;
    change->source = source;
    Queue(std::move(change));
}
