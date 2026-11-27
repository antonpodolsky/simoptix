// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hover_tally.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "quantile.h"

namespace {

    constexpr double kMoveS = 0.001;  // a correction that moves less isn't applied again

}

HoverTally::HoverTally(const AutoSyncValues& kept, std::vector<std::string> sources)
    : kept_(kept),
      sources_(std::move(sources)),
      ticks_(sources_.size()) {}

void HoverTally::AddTick(int source, double lag, float riseDb) {
    ticks_[source].push_back({lag, riseDb});
}

// The most of the source's ticks whose lags lie within kAgreeS of each other.
HoverTally::Agreeing HoverTally::Agree(int source) const {
    std::vector<Tick> ticks = ticks_[source];
    std::sort(ticks.begin(), ticks.end(), [](const Tick& a, const Tick& b) { return a.lag < b.lag; });
    Agreeing best;

    for (size_t first = 0; first < ticks.size(); first++) {
        size_t last = first;

        while (last + 1 < ticks.size() && ticks[last + 1].lag - ticks[first].lag <= kAgreeS) {
            last++;
        }

        std::vector<double> lags;
        float rise = 0;

        for (size_t i = first; i <= last; i++) {
            lags.push_back(ticks[i].lag);
            rise += ticks[i].riseDb;
        }

        const int count = static_cast<int>(lags.size());
        rise /= count;

        if (count > best.count || (count == best.count && rise > best.riseDb)) {
            best = {count, Quantile(lags, 0.5), rise};
        }
    }

    return best;
}

// The game's sound: the source whose ticks agree the most (then the sharpest), once kAgreeing of them
// agree. -1 if none does yet.
int HoverTally::GameSound(Agreeing* game) const {
    int best = -1;

    for (size_t i = 0; i < ticks_.size(); i++) {
        if (ticks_[i].empty()) {
            continue;
        }

        const Agreeing a = Agree(static_cast<int>(i));

        if (best < 0 || a.count > game->count || (a.count == game->count && a.riseDb > game->riseDb)) {
            best = static_cast<int>(i);
            *game = a;
        }
    }

    return best >= 0 && game->count >= kAgreeing ? best : -1;
}

bool HoverTally::Update() {
    ticksHeard_++;
    Agreeing agreeing;
    const int game = GameSound(&agreeing);

    if (game < 0) {
        return false;
    }

    // The ticks fall `lag` after their highlights: the sound moves back by as much.
    AutoSyncValues values;
    values.hasSound = true;
    values.gameSound = sources_[game];
    values.soundCorrection = -agreeing.lag;
    const AutoSyncValues& before = values_.hasSound ? values_ : kept_;
    const bool moved = !before.hasSound || values.gameSound != before.gameSound
                       || std::fabs(values.soundCorrection - before.soundCorrection) >= kMoveS;
    values_ = values;
    return moved;
}

bool HoverTally::Done() const {
    Agreeing agreeing;
    return (GameSound(&agreeing) >= 0 && agreeing.count >= kSettled) || ticksHeard_ >= kGiveUpAfter;
}

std::string HoverTally::Describe() const {
    Agreeing agreeing;
    const int game = GameSound(&agreeing);
    char line[256];

    if (game < 0) {
        snprintf(line, sizeof(line), "after %d ticks: the game's sound not found yet", ticksHeard_);
    } else {
        snprintf(
            line,
            sizeof(line),
            "after %d ticks: '%s' has them %.0f ms after (%d agree), so it moves %+.0f ms",
            ticksHeard_,
            sources_[game].c_str(),
            agreeing.lag * 1000,
            agreeing.count,
            values_.soundCorrection * 1000
        );
    }

    return line;
}
