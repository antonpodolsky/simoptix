// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The layer's log: %LOCALAPPDATA%\simoptix\openxr_frame_source_<date>_<time>.log, one per game start, named
// by LocalFiles (shared\local_files.h), which keeps the newest. Like all of SimOptix's logs it follows
// Debug, here the Debug of VR Mirror's source that reads the frames: its lines wait in memory until that
// reader comes, then go to the file if Debug is on and away if it is off. Without a reader they never reach
// a file. Failures (CoreLogError) are always written. The file is made at its first line, and the oldest
// logs are deleted then.
//
// Its first line says that Windows loaded the DLL into the game, before any OpenXR step. Once started,
// CoreLog and CoreLogError (shared\log.h) write the rest, with the time of day to the millisecond, from any
// thread. The game's threads never wait for it: a line is copied into memory (skipped, and counted, if a
// writer is busy or the memory is full), and a thread of its own writes it to disk. That thread keeps the
// layer's DLL loaded until the game exits, so it can never run after the DLL is unloaded. It doesn't know
// what the layer logs or why.
#pragma once

// Keeps this start's first line in memory: the date, the process id and the program's path, and names the
// log by that time. Safe inside DllMain: Windows' own calls only, no threads, no other DLLs.
void NoteDllLoaded();

// Starts the writing thread and points CoreLog and CoreLogError at it. False if that fails; they then write
// nowhere.
bool StartLayerLog();

// Whether VR Mirror reads the frames, and with Debug on (OpenXrPublisher::ReaderNews). A reader decides
// what happens to the lines, the held ones too; once it leaves, its word stands until the next one comes.
void FollowReaderDebug(bool there, bool debug);
