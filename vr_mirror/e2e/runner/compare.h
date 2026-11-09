// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Look-ahead against Instant: for each scenario that ran at both smoothings, its measures side by
// side, and a fail where the look-ahead made one worse than Instant. Knows
// nothing about running the tool or measuring: it reads the results' values (Result::values).
#pragma once

#include "analysis.h"
#include "scenarios.h"

// One result per scenario run at every smoothing, named after it (its Instant name); `runs` and
// `results` are in the same order.
std::vector<Result> CompareSmoothings(const std::vector<Scenario>& runs, const std::vector<Result>& results);
