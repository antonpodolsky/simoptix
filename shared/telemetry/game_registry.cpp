// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "shared/telemetry/game_registry.h"

#include "shared/telemetry/game_adapter.h"

// A game whose SDK can't be shipped with the source is compiled only when its SDK is found (see
// overlays\obs_plugin\build.bat), and defines HAVE_<GAME> then.
#ifdef HAVE_LMU
#include "shared/telemetry/games/lmu/lmu_telemetry.h"
#endif

namespace {

    GameAdapter* const kNone = nullptr;  // stands in for an empty list (C++ has no empty arrays)

}

GameAdapter* const* RegisteredGames(size_t* count) {
    // One line per game, tried in this order.
    static GameAdapter* const games[] = {
#ifdef HAVE_LMU
        LmuAdapter(),
#endif
        kNone,
    };

    *count = sizeof(games) / sizeof(games[0]) - 1;
    return games;
}
