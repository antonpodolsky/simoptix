// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// CrashDump: writes a minidump when OBS crashes (an access violation and the like), so a crash can be
// traced to its line: OBS's own crash handler doesn't always get to run. Each plugin installs it when
// it loads; the count lives in the process's SharedMemory, so with both plugins installed one crash
// still gives one dump, and at most two are written per OBS session. Dumps go to
// %LOCALAPPDATA%\simoptix\, named obs_<date>_<time>_<code>_<plugin>.dmp after the plugin
// whose handler wrote it (the first to see the crash, not necessarily the one at fault).
//
// The handler sees exceptions before anything handles them, so an exception OBS or a driver recovers
// from may leave a dump too.
#pragma once

// `plugin`: a short name for the dump's file name (a string literal: it is kept). obs_module_load.
void InstallCrashDump(const wchar_t* plugin);
