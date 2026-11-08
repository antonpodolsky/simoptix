// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// AudioSync: keeps every audio source in step with the VR Mirror source's picture, which runs ProgramDelay
// behind the game (look-ahead smoothing): each audio source's sync offset becomes its own offset plus
// that delay. LMU's sound keeps its own offset (-120 ms: it trails the picture), so it ends up that
// much earlier than the rest, without this knowing which source it is.
//
// The shift each source got is kept in its private settings, which OBS saves with the scene, so a
// source's own offset is always its current offset minus that shift: it comes back exactly when the
// delay goes back to 0 (the source hidden), and after OBS was closed with the offsets shifted.
// The offsets shown in OBS include the shift; changing one there changes the source's own offset.
//
// The OBS calls run on OBS's UI thread (a queued task); a tick callback on the graphics thread only
// checks the delay and the time: on a change of the delay, every 2 s while it isn't 0 (audio sources
// added meanwhile), and for 30 s after loading (shifts left from before). No work otherwise.
#pragma once

#include <cstdint>

namespace AudioSync {
    void Start();  // obs_module_load
    void Stop();   // obs_module_unload (offsets stay as they are: each source carries its shift)
}

// Logic: the sync offset (ns) a source gets for `shift`, from its `current` offset and the shift it
// already has (`applied`).
inline int64_t ShiftedSyncOffset(int64_t current, int64_t applied, int64_t shift) {
    return current - applied + shift;
}
