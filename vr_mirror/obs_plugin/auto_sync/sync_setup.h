// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// SyncSetup: what auto sync's values depend on, as the plugin can see it. The game's frame rate and image
// size (LMU's graphics settings, the headset's refresh rate), how many times the game's frames have started
// (a restart of LMU or of its VR session), each audio source in OBS with its settings, and Windows' default
// output device. When any of it changes, the values measured before may no longer hold.
//
// SyncSetupWatch reads it: the frame source's own numbers, the frame ring's count of restarts (it sees
// every frame, however short the pause), OBS's list of sources, and Windows' audio devices. Thin glue. One
// thread, which has COM initialized (Windows' audio devices).
#pragma once

#include <cstdint>
#include <string>

class FrameRing;
class FrameSource;

struct SyncSetup {
    int frameRate = 0;                         // the game's frames a second, rounded to 5 (0: none yet)
    uint32_t imageWidth = 0, imageHeight = 0;  // the game's image (pixels; 0: none yet)
    uint64_t gameStarts = 0;                   // the game's frames started after a pause of 2 s or more
    std::string audio;                         // each audio source's name and settings
    std::string outputDevice;                  // Windows' default output device's ID
};

// What changed from `before` to `now`, for the log ("the game's frame rate, 90 to 72"); empty if nothing.
std::string SetupChange(const SyncSetup& before, const SyncSetup& now);

class SyncSetupWatch {
public:
    // The setup now, from `source`'s frames as `ring` copies them, OBS's audio sources and Windows' default
    // output device. Read every few seconds: the first frame rate counts at once, and a new one once it has
    // held for kSteadyReads reads.
    SyncSetup Read(FrameSource* source, const FrameRing& ring);

private:
    SyncSetup setup_;
    int seenRate_ = 0, seenReads_ = 0;  // the frame rate read last, and how many reads in a row had it
};
