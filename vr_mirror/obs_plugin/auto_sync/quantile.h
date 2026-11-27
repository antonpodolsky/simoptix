// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Quantile: the value at a fraction of a list of measurements, for auto sync's tallies. Logic only.
#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

// The value at `fraction` (0-1) of `values`, linearly between the two around it; 0 for none.
inline double Quantile(std::vector<double> values, double fraction) {
    if (values.empty()) {
        return 0;
    }

    std::sort(values.begin(), values.end());
    const double place = fraction * (values.size() - 1);
    const size_t below = static_cast<size_t>(place);
    const size_t above = std::min(below + 1, values.size() - 1);
    return values[below] + (place - below) * (values[above] - values[below]);
}
