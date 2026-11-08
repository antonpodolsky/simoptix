// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// SessionWorker: starts and stops a VR Mirror source's frame source (FrameSource: the Pimax session,
// or the OpenXR layer's reader) and copy thread (FrameRing) on a thread of its own, so OBS's graphics
// thread never waits for them.
// Starting takes about a third of a second (the runtime, a session, and 300 ms for Pimax to create
// its buffer); stopping waits for the poll and copy threads (which can be inside a reconnect's
// 300 ms sleep). Done on the graphics thread, either stalled OBS's whole output meanwhile.
//
// The graphics thread asks (Start, Stop) and reads State, and uses the FrameSource and FrameRing
// only while it reads kRunning. Knows nothing about OBS: logging goes through CoreLog.
#pragma once

#include <atomic>
#include <thread>

class FrameRing;
class FrameSource;

class SessionWorker {
public:
    enum State : int { kStopped, kStarting, kRunning, kStopping };

    SessionWorker(FrameSource* source, FrameRing* ring) : source_(source), ring_(ring) {}
    ~SessionWorker() { StopNow(); }

    // Graphics thread. Start acts only from kStopped, Stop only from kRunning; both return at once.
    void Start();
    void Stop();
    State Get() const { return State(state_.load()); }
    // True once after a start that failed (back at kStopped; the reason is logged).
    bool TakeFailure() { return failed_.exchange(false); }

    // Waits for a start or stop in progress, then stops what runs (the source is being destroyed).
    void StopNow();

private:
    void Join();

    FrameSource* source_;
    FrameRing* ring_;
    std::thread thread_;  // started and joined by the graphics thread (StopNow: by OBS's destroy)
    std::atomic<int> state_{kStopped};
    std::atomic<bool> failed_{false};
};
