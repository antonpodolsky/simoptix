// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Logging for the shared core. The host decides where messages go (console, OBS log).
#pragma once

using CoreLogFn = void (*)(const char* message);

// nullptr (the default) prints to stdout.
void SetCoreLog(CoreLogFn fn);

// printf-style; a trailing newline is added if missing.
void CoreLog(const char* fmt, ...);
