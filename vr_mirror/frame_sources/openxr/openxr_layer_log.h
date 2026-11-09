// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The layer's log: %LOCALAPPDATA%\simoptix\openxr_frame_source_<date>_<time>.log, one per game start, named
// by LocalFiles (shared\local_files.h), which keeps the newest. Its first line says that Windows loaded the
// DLL into the game, before any OpenXR step. Once started, CoreLog (shared\log.h) writes the rest there, with
// the time of day to the millisecond, from any thread. The game's threads never wait for it: a line is copied
// into memory (skipped, and counted, if a writer is busy or the memory is full), and a thread of its own
// writes it to disk. That thread deletes the oldest logs first, then sleeps until there is a line. It keeps
// the layer's DLL loaded until the game exits, so it can never run after the DLL is unloaded. It doesn't know
// what the layer logs or why.
#pragma once

// Makes this start's log with its first line: the date, the process id and the program's path. If the game
// starts and no new log appears, the DLL was never loaded (blocked by anti-cheat, say). A log with this
// line alone means OpenXR never called the layer. Safe inside DllMain: Windows' own calls only, no
// threads, no other DLLs.
void NoteDllLoaded();

// Opens this start's log (made by NoteDllLoaded, or here if it didn't run), starts the writing thread and
// points CoreLog at it. False if either fails; CoreLog then writes nowhere.
bool StartLayerLog();
