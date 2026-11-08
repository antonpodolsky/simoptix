// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// PvrSession: the PVR session with the Pimax runtime: opening and closing it (with loading and
// unloading Pimax's client library, declared in pvr_interface.h), the display and eye
// geometry it reports, re-creating it when another VR app takes over, and the call lock. The client
// library isn't safe to call from several threads at once, so every runtime call goes through a
// Calls object, which holds the lock for a few calls in a row and times each into the caller's
// footprint stats. Knows nothing about mirrors, frames or poses.
#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "common/footprint.h"
#include "common/game_frame.h"
#include "common/rot.h"
#include "vr_mirror/frame_sources/pimax/pvr_interface.h"

class PvrSession {
public:
    ~PvrSession() { Close(); }

    // Loads the runtime and opens a session; reads the display and eye geometry.
    bool Open(std::string* error);
    // Closes the session and unloads the runtime. Safe to call more than once.
    void Close();

    LUID AdapterLuid() const { return luid_; }
    EyeInfo Eye(int eye) const { return eyes_[eye]; }
    double Period() const { return period_; }

    // The call lock, held for its lifetime; every runtime call after Open goes through one. Each call
    // adds its duration to the CallStats it is given (owned by the calling thread).
    class Calls {
    public:
        explicit Calls(PvrSession& s) : s_(s), lock_(s.mutex_) {}
        // False while the session is being re-created (Recreate): no session to call.
        bool HasSession() const { return s_.hmd_ != nullptr; }
        // The runtime and the session's headset, for the mirror's emergency switch-off.
        const PvrInterface* Runtime() const { return s_.pvr_; }
        PvrHmd Hmd() const { return s_.hmd_; }
        double Time(CallStats& stats);  // the runtime's clock (source seconds)
        // ...with the performance counter read just before and after the call (seconds), for
        // ClockSync; the caller counts the call.
        double Time(double* qpcBefore, double* qpcAfter);
        // The head orientation the runtime reports or predicts for source time t; false if not tracked.
        bool Orientation(double t, Quat* q, CallStats& stats);
        double NextDisplaySlot(CallStats& stats);  // the display slot a game would render for now
        bool ShouldQuit(CallStats& stats);         // the server asks this session to close
        int64_t Int64Config(const char* key, CallStats& stats);
        int SetIntConfig(const char* key, int value);  // config writes are rare: not timed

    private:
        PvrSession& s_;
        std::lock_guard<std::mutex> lock_;
    };

    // The runtime's clock without a session lock held by the caller (before the poll thread has
    // measured its offset); 0 when closed.
    double Time(CallStats& stats);

    // Closes the session at the server's request, waits for the other app to take over and opens a
    // new one, retrying until it opens or `running` goes false. `reenable` runs under the call lock
    // right after the new session opens (the mirror switches itself back on). Poll thread.
    template <class Reenable>
    void Recreate(const std::atomic<bool>& running, Reenable&& reenable) {
        {
            std::lock_guard<std::mutex> calls(mutex_);
            DestroySession();
        }

        for (int i = 0; i < 30 && running; i++) {
            Sleep(100);  // let the other app become the active one
        }
        while (running) {
            if (CreateSession()) {
                Calls calls(*this);
                reenable(calls);
                return;
            }

            for (int i = 0; i < 20 && running; i++) {
                Sleep(100);
            }
        }
    }

private:
    bool CreateSession();   // under no lock: hmd_ is set under the call lock
    void DestroySession();  // under the call lock

    const PvrInterface* pvr_ = nullptr;  // the client library's function table, while open
    PvrHmd hmd_ = nullptr;               // the session's headset (under mutex_)
    LUID luid_ = {};
    EyeInfo eyes_[2];
    double period_ = 1.0 / 90;
    mutable std::mutex mutex_;  // the call lock
};
