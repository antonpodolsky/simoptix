// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// LocalFiles: every file SimOptix writes under %LOCALAPPDATA%\simoptix, by kind: its folder, its name and how
// many of the kind are kept. A file's name is its kind's name and the local time it was made, then the
// writer's own ending (trace_20261109_133311_ticks.csv), so names sort by age. Like OBS with its own logs,
// each kind keeps its newest files, and the oldest beyond the kind's count are deleted as new ones start.
// The files of one kind made in the same second are one (a trace's _ticks and _pimax files) and go
// together. A name of any other form is never touched. Knows nothing about what the files hold.
#pragma once

#include <cstddef>

enum class LocalFileKind { kFrameTrace, kCrashDump, kLayerLog };

// How many files of `kind` are kept.
int LocalFilesKept(LocalFileKind kind);

// The path of a new file of `kind`, made now, without its ending: "<folder>\<name>_<yyyymmdd>_<hhmmss>".
// The caller adds the ending ("_ticks.csv"). Creates the folder. False if the path doesn't fit in `size`
// characters. Windows' own calls only and nothing allocated, so it is safe in a crash handler and in
// DllMain.
bool NewLocalFilePath(LocalFileKind kind, wchar_t* path, size_t size);

// Deletes the oldest files of `kind` until `room` more fit within its count, and returns how many it
// deleted (the files of one second count once). A file in use stays.
int PruneLocalFiles(LocalFileKind kind, int room);

// The full path of the newest file of `kind`. False if there is none.
bool NewestLocalFile(LocalFileKind kind, wchar_t* path, size_t size);
