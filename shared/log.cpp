// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "shared/log.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {
    CoreLogFn g_log = nullptr;
    CoreLogFn g_logError = nullptr;  // failures' own place (SetCoreLogError); null: g_log
    thread_local const std::atomic<bool>* t_gate = nullptr;  // this thread's gate (ScopedLogGate)

    void Write(CoreLogFn to, const char* fmt, va_list args) {
        char message[1024];
        vsnprintf(message, sizeof(message), fmt, args);

        const size_t len = strlen(message);
        if (len > 0 && message[len - 1] == '\n') {
            message[len - 1] = '\0';
        }

        if (to) {
            to(message);
        } else {
            printf("%s\n", message);
            fflush(stdout);
        }
    }
}

void SetCoreLog(CoreLogFn fn) {
    g_log = fn;
}

void SetCoreLogError(CoreLogFn fn) {
    g_logError = fn;
}

void CoreLog(const char* fmt, ...) {
    if (!CoreLogWanted()) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    Write(g_log, fmt, args);
    va_end(args);
}

void CoreLogError(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Write(g_logError ? g_logError : g_log, fmt, args);
    va_end(args);
}

bool CoreLogWanted() {
    return !t_gate || t_gate->load(std::memory_order_relaxed);
}

const std::atomic<bool>* CurrentLogGate() {
    return t_gate;
}

ScopedLogGate::ScopedLogGate(const std::atomic<bool>* wanted) : previous_(t_gate) {
    t_gate = wanted;
}

ScopedLogGate::~ScopedLogGate() {
    t_gate = previous_;
}
