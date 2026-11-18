// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "auto_record.h"

#include <windows.h>
#include <obs-module.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

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
        bool on = false;                              // the tick callback is added and the reader held
        std::vector<const std::atomic<bool>*> debug;  // the Debug setting of each source it is on for
        // Set before the tick callback is added, then only read.
        VoidFn start = nullptr, stop = nullptr;
        BoolFn active = nullptr;

        // Graphics thread (the tick callback).
        RaceRecorder recorder;
        double nextStep = 0;  // performance-counter seconds, on the view's clock (Telemetry::Shown)
        bool logged = false;  // loggedKind and loggedPhase hold what was logged last
        SessionKind loggedKind = SessionKind::Other;
        SessionPhase loggedPhase = SessionPhase::Unknown;
        std::atomic<bool> logging{false};    // one of its sources has Debug on: this thread's log gate
        std::atomic<bool> heldDebug{false};  // ...as told to the telemetry (Telemetry::HoldDebug)
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

    // Whether one of the sources it is on for has Debug on. The list is only skipped, never waited for,
    // while the UI thread changes it: the answer comes again at the next step.
    bool AnyDebug(State& st, bool previous) {
        std::unique_lock<std::mutex> lock(st.users, std::try_to_lock);

        if (!lock.owns_lock()) {
            return previous;
        }

        for (const std::atomic<bool>* debug : st.debug) {
            if (debug->load()) {
                return true;
            }
        }

        return false;
    }

    void Tick(void*, float) {
        State& st = TheState();

        // Everything on the view's clock (now less the VR Mirror source's published delay), so the race
        // starts and ends in the recording where the recorded video shows it.
        const double now = Telemetry::Shown();
        if (now < st.nextStep) {
            return;
        }
        st.nextStep = now + kStepEvery;
        st.logging = AnyDebug(st, st.logging);
        const ScopedLogGate gate(&st.logging);

        // The telemetry it holds logs while one of its sources has Debug on.
        if (st.heldDebug.exchange(st.logging) != st.logging) {
            Telemetry::HoldDebug(st.logging);
        }

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

void AutoRecord::Enable(const std::atomic<bool>* debug) {
    State& st = TheState();
    std::lock_guard<std::mutex> g(st.users);
    st.debug.push_back(debug);
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
            CoreLogError("LMU race auto-record: OBS's frontend API isn't there, so races can't be recorded");
            return;
        }
    }

    st.recorder.Reset();
    st.nextStep = 0;
    st.logged = false;

    // Its source's Debug, before the telemetry's reader starts, so its first pass logs as Debug says. Tick
    // follows it from then on. This source is the only one in the list.
    st.logging = debug->load();
    st.heldDebug = st.logging.load();
    if (st.heldDebug) {
        Telemetry::HoldDebug(true);
    }

    Telemetry::Acquire();
    obs_add_tick_callback(Tick, nullptr);
    st.on = true;
    CoreLog("LMU race auto-record: on");
}

void AutoRecord::Disable(const std::atomic<bool>* debug) {
    State& st = TheState();
    std::lock_guard<std::mutex> g(st.users);
    const auto found = std::find(st.debug.begin(), st.debug.end(), debug);

    if (found != st.debug.end()) {
        st.debug.erase(found);
    }

    if (st.count == 0 || --st.count > 0 || !st.on) {
        return;
    }
    obs_remove_tick_callback(Tick, nullptr);

    if (st.heldDebug.exchange(false)) {
        Telemetry::HoldDebug(false);
    }

    Telemetry::Release();
    st.on = false;
    CoreLog("LMU race auto-record: off");
}
