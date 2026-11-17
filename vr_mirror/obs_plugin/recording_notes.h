// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// RecordingNotes: a text file beside each OBS recording, `<recording>.simoptix.txt`, saying what each VR
// Mirror source ran with: the plugin's version, and per source its settings, the game's eye and its frame
// trace. It is written when the recording starts, and a line is added whenever one of these changes while
// it records, with the video time. OBS's recording events come from its frontend API
// (obs-frontend-api.dll), looked up at run time as race auto-record does.
//
// Threads: everything it keeps belongs to OBS's UI thread, where OBS reports recordings. Note and Forget
// may be called from any thread: they queue the change to the UI thread and never wait. They are called
// only when something changes, never per frame.
#pragma once

namespace RecordingNotes {

    // Module load and unload: follow OBS's recording events, and stop following them.
    void Start();
    void Stop();

    // What `source` (a key: its MirrorView) now runs with, under `what` ("settings", "eye", "frame trace"),
    // shown as `name` (its frame source). Any thread.
    void Note(const void* source, const char* name, const char* what, const char* text);
    // The source is gone. Any thread.
    void Forget(const void* source);

}
