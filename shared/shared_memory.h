// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// SharedMemory: the few numbers the plugins in one OBS process share without loading or calling each
// other. It is a small named memory block, one per process ("Local\simoptix_<pid>"):
// the first plugin to ask creates it (zeroed), every other one opens the same block. A plugin
// installed alone just has the block to itself, so every value reads as its zero default.
//
// Each plugin compiles its own copy of this code, so a block made by one build may be read by another:
// fields are only ever appended, never moved or removed. Read and write them with the Interlocked
// functions (any thread).
//
// It knows nothing of what the numbers mean: program_delay.h and crash_dump.h do.
#pragma once

#include <windows.h>

struct SharedMemory {
    volatile LONG64 programDelayUs;  // program_delay.h: microseconds, 0 when nothing delays the picture
    volatile LONG crashDumps;        // crash_dump.h: minidumps written in this process so far
};

// The process's block. Never null: if the named block can't be made, a block private to this plugin
// stands in (the plugins then share nothing, as if each were installed alone). Never freed.
SharedMemory* TheSharedMemory();
