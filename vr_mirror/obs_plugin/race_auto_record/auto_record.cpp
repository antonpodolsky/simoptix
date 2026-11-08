// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "auto_record.h"

#include <windows.h>
#include <obs-module.h>

#include <mutex>

#include "race_recorder.h"
#include "shared/log.h"
#include "shared/telemetry/telemetry.h"

namespace {

    constexpr double kStepEvery = 0.25;  // s

    // OBS's frontend API, from obs-frontend-api.dll.
    using VoidFn = void (*)(void);
    using BoolFn = bool (*)(void);

    struct State {
        // Enable and Disable (UI thread).
        std::mutex users;
        int count = 0;
        bool on = false;  // the tick callback is added and the reader held
        // Set before the tick callback is added, then only read.
        VoidFn start = nullptr, stop = nullptr;
        BoolFn active = nullptr;

        // Graphics thread (the tick callback).
        RaceRecorder recorder;
        double nextStep = 0;  // performance-counter seconds, on the picture's clock (Telemetry::Shown)
        bool logged = false;  // loggedKind and loggedPhase hold what was logged last
        SessionKind loggedKind = SessionKind::Other;
        SessionPhase loggedPhase = SessionPhase::Unknown;
    };

    // Never destroyed, like the telemetry reader.
    State& TheState() {
        static State* state = new State();
        return *state;
    }

    void StartRecording(void*) {
        TheState().start();
    }
    void StopRecording(void*) {
        TheState().stop();
    }

    void Tick(void*, float) {
        State& st = TheState();

        // Everything on the picture's clock (now less the VR Mirror source's published delay), so the race
        // starts and ends in the recording where the recorded picture shows it.
        const double now = Telemetry::Shown();
        if (now < st.nextStep) {
            return;
        }
        st.nextStep = now + kStepEvery;

        const SessionState session = Telemetry::SessionAt(now);
        if (session.connected && session.t > 0
            && (!st.logged || session.kind != st.loggedKind || session.phase != st.loggedPhase)) {
            st.logged = true;
            st.loggedKind = session.kind;
            st.loggedPhase = session.phase;
            CoreLog(
                "LMU race auto-record: session %s, phase %s",
                SessionKindName(session.kind),
                SessionPhaseName(session.phase)
            );
        }

        const RaceInput in = {now, session, st.active()};
        const char* note = nullptr;
        const RaceAction action = st.recorder.Step(in, &note);
        if (note) {
            CoreLog("LMU race auto-record: %s", note);
        }

        // OBS starts and stops recording on its UI thread; queued, so this thread doesn't wait for it.
        if (action == RaceAction::Start) {
            obs_queue_task(OBS_TASK_UI, StartRecording, nullptr, false);
        } else if (action == RaceAction::Stop) {
            obs_queue_task(OBS_TASK_UI, StopRecording, nullptr, false);
        }
    }

}

void AutoRecord::Enable() {
    State& st = TheState();
    std::lock_guard<std::mutex> g(st.users);
    if (st.count++ > 0) {
        return;
    }
    if (!st.start) {
        if (HMODULE frontend = GetModuleHandleW(L"obs-frontend-api.dll")) {
            st.start = reinterpret_cast<VoidFn>(GetProcAddress(frontend, "obs_frontend_recording_start"));
            st.stop = reinterpret_cast<VoidFn>(GetProcAddress(frontend, "obs_frontend_recording_stop"));
            st.active = reinterpret_cast<BoolFn>(GetProcAddress(frontend, "obs_frontend_recording_active"));
        }

        if (!st.start || !st.stop || !st.active) {
            st.start = nullptr;
            CoreLog("LMU race auto-record: OBS's frontend API isn't there, so races can't be recorded");
            return;
        }
    }

    st.recorder.Reset();
    st.nextStep = 0;
    st.logged = false;
    Telemetry::Acquire();
    obs_add_tick_callback(Tick, nullptr);
    st.on = true;
    CoreLog("LMU race auto-record: on");
}

void AutoRecord::Disable() {
    State& st = TheState();
    std::lock_guard<std::mutex> g(st.users);
    if (st.count == 0 || --st.count > 0 || !st.on) {
        return;
    }
    obs_remove_tick_callback(Tick, nullptr);
    Telemetry::Release();
    st.on = false;
    CoreLog("LMU race auto-record: off");
}
