// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Guarded: runs the body of a callback that a C program calls (OBS, the OpenXR loader) so that no
// exception gets out. An exception that escapes into C code ends the host on the spot. Guarded catches
// it, logs it once for that call site, and returns the callback's empty answer (nothing, 0, nullptr or
// false). Knows nothing about what the callback does.
#pragma once

#include <atomic>

#include "shared/log.h"

template <class F>
auto Guarded(const char* what, F&& body) -> decltype(body()) {
    using Result = decltype(body());
    static std::atomic<bool> logged{false};  // one line for each call site, not one for each call

    try {
        return body();
    } catch (...) {
        if (!logged.exchange(true)) {
            CoreLog("%s stopped by an unexpected error", what);
        }
        return Result();
    }
}
