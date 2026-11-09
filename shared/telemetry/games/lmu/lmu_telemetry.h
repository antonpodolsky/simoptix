// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// LmuTelemetry: Le Mans Ultimate's adapter (game_adapter.h). Reads the player car's telemetry and the
// session from LMU's shared memory: Studio 397's official interface for outside tools
// (Support\SharedMemoryInterface in the game folder; the build includes it from there, its licence
// doesn't allow copying it into this repo, and builds without it leave LMU out). Reading it is what
// SimHub, CrewChief and the like do; nothing is loaded into the game.
//
// It hands over every update in the telemetry model's units, axes and terms (lmu_convert.h), so nothing
// past this folder knows LMU's. Read sleeps until LMU signals new data (200 ms at most), takes LMU's
// lock only long enough to copy one sample (and skips an update rather than wait if the game holds
// it), and hands over a car sample for each new physics step. 5 s without an update means the game has
// quit. Each step carries LMU's session type and track as the session's key, and its session clock, from
// which the reader counts driving sessions.
//
// Only one reader per OBS process opens LMU (telemetry_reader.h). LMU's lock lets several readers take
// turns, but its update signal isn't documented as waking more than one waiter per update (the fake in
// shared\telemetry\games\lmu\fake wakes one), so two readers might each see only some of the updates.
//
// Raw data only: no slip maths, filtering, learning or decisions here. LMU leaves the tyre forces and
// the tyre load at zero (measured in LMU), so they aren't copied; the vertical tyre deflection
// stands in for load.
#pragma once

class GameAdapter;

// The adapter, made on first use and never destroyed (game_registry.cpp lists it).
GameAdapter* LmuAdapter();
