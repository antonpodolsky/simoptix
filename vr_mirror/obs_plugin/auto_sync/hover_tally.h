// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// HoverTally: auto sync's decisions on the game's sound. It collects, for each tick an audio source has, how
// much later than its highlight the tick came, and says what to apply: which source has the game's sound,
// and how much it has to move so its ticks fall on their highlights. The game's sound is the source whose
// ticks agree, tick after tick, within kAgreeS: clicks, music and voices have sharp sounds too, and the
// image often changes before them, but not at the same lag every time. So the game's sound moves only once
// kAgreeing ticks agree. It says when the values moved enough to apply again, and when to stop: once
// kSettled ticks agree, or after kGiveUpAfter ticks.
//
// Logic only: the lags come in. Knows nothing about OBS, frames, sound or threads.
#pragma once

#include <string>
#include <vector>

#include "auto_sync_values.h"

class HoverTally {
public:
    static constexpr int kAgreeing = 3, kSettled = 5, kGiveUpAfter = 60;
    static constexpr double kAgreeS = 0.03;

    // Starts from the values kept from before, with the audio sources' names.
    HoverTally(const AutoSyncValues& kept, std::vector<std::string> sources);

    // A tick with a highlight before it, then Update. `lag` in seconds after the highlight.
    void AddTick(int source, double lag, float riseDb);
    // After each tick, with a highlight or not: true if the values to apply changed (Values).
    bool Update();

    bool Done() const;
    // The game's sound once found: only its values are set.
    const AutoSyncValues& Values() const { return values_; }
    // One line for the log: the game's sound and its lag, from how many ticks.
    std::string Describe() const;

private:
    struct Tick {
        double lag;
        float riseDb;
    };

    // A source's ticks that agree: how many, their median lag and mean rise.
    struct Agreeing {
        int count = 0;
        double lag = 0;
        float riseDb = 0;
    };

    Agreeing Agree(int source) const;
    int GameSound(Agreeing* game) const;

    AutoSyncValues kept_, values_;
    std::vector<std::string> sources_;
    std::vector<std::vector<Tick>> ticks_;
    int ticksHeard_ = 0;
};
