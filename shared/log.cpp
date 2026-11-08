// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "shared/log.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {
    CoreLogFn g_log = nullptr;
}

void SetCoreLog(CoreLogFn fn) {
    g_log = fn;
}

void CoreLog(const char* fmt, ...) {
    char message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    const size_t len = strlen(message);
    if (len > 0 && message[len - 1] == '\n') {
        message[len - 1] = '\0';
    }

    if (g_log) {
        g_log(message);
    } else {
        printf("%s\n", message);
        fflush(stdout);
    }
}
