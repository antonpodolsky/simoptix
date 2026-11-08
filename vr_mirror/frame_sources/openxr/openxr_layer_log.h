// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The layer's log file: %LOCALAPPDATA%\simoptix\openxr_frame_source.log, one per game start
// (the one before is kept as openxr_frame_source.prev.log). Once started, CoreLog (shared\log.h) writes
// there, with the time of day to the millisecond, from any thread. The game's threads never wait for it: a
// line is copied into memory (skipped, and counted, if a writer is busy or the memory is full), and a thread
// of its own writes it to disk. That thread sleeps until there is a line, and keeps the layer's DLL loaded
// until the game exits, so it can never run after the DLL is unloaded. It doesn't know what the layer logs or
// why.
#pragma once

// Opens the file, starts the writing thread and points CoreLog at it. False if either fails; CoreLog
// then writes nowhere.
bool StartLayerLog();

// Adds one line, with the date, the process id and the program's path, to openxr_frame_source_loads.log
// next to the log, which is never cleared: a record of every time Windows loaded the DLL into the game,
// before any OpenXR step. If the game starts and no line appears, the DLL was never loaded (blocked by
// anti-cheat, say). Safe inside DllMain: Windows' own file calls only, no threads, no other DLLs.
void NoteDllLoaded();
