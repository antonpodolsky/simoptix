// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The games the overlays can read, in the order the telemetry reader tries them. The one list that
// names games: the reader and the overlays only see GameAdapter.
//
// Any thread: the list is made on first use and never changes.
#pragma once

#include <cstddef>

class GameAdapter;

// The registered adapters: `*count` of them. Empty in a build without any game's SDK.
GameAdapter* const* RegisteredGames(size_t* count);
