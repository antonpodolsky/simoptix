// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Logging for the shared core. The host decides where messages go (console, OBS log).
//
// A host can gate a thread's lines on a flag: in the OBS plugin, the Debug setting of the VR Mirror source
// the thread works for, so a source logs only while it has Debug on. A thread with no gate logs
// everything (the mirror app, e2e, the tools). Failures go through CoreLogError, which logs whatever the
// gate says, so a user without Debug still has something to report.
#pragma once

#include <atomic>

using CoreLogFn = void (*)(const char* message);

// nullptr (the default) prints to stdout.
void SetCoreLog(CoreLogFn fn);
// Where failures (CoreLogError) go, for a host that treats them apart from its normal lines (the OpenXR
// layer writes them whatever Debug says). nullptr (the default): where CoreLog's go.
void SetCoreLogError(CoreLogFn fn);

// printf-style; a trailing newline is added if missing. Nothing while this thread's gate is closed.
void CoreLog(const char* fmt, ...);
// The same for a failure: logged whatever the gate says.
void CoreLogError(const char* fmt, ...);

// Whether this thread's normal lines are logged now: no gate, or its flag is true.
bool CoreLogWanted();

// This thread's gate (null: none), for a thread that starts another to hand it on (ScopedLogGate).
const std::atomic<bool>* CurrentLogGate();

// For its scope, this thread logs normal lines only while `*wanted` is true (null: no gate). It puts back
// the gate it found when it ends.
class ScopedLogGate {
public:
    explicit ScopedLogGate(const std::atomic<bool>* wanted);
    ~ScopedLogGate();
    ScopedLogGate(const ScopedLogGate&) = delete;
    ScopedLogGate& operator=(const ScopedLogGate&) = delete;

private:
    const std::atomic<bool>* previous_;
};
