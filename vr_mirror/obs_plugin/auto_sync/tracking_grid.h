// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// TrackingGrid: directions in the headset's tracking space, on which a grey copy of a frame is laid through
// the head pose the frame was drawn with. What is fixed in tracking space, such as the cockpit or a game's
// menu panel, then stays put on the grid however the head moves.
//
// Logic only: the frame and its pose come in. Knows nothing about D3D, OBS or threads.
#pragma once

#include <cstdint>
#include <vector>

#include "grey_frame.h"

// A grid of directions: square cells `cellDeg` wide, from `leftDeg` (negative: left of straight ahead) and
// `topDeg` (positive: up), `columns` across and `rows` down.
struct TrackingGrid {
    float cellDeg, leftDeg, topDeg;
    int columns, rows;

    int Cells() const { return columns * rows; }
};

// A grid cell that the frame doesn't reach.
constexpr uint8_t kGridUnseen = 255;

// Each cell's direction in tracking space (x, y, z, cell after cell), worked out once for LayOnGrid.
std::vector<float> GridDirections(const TrackingGrid& grid);

// The grey copy `frame` (from `layout`) on `grid`, whose cells point along `directions` (GridDirections),
// as 0-254, kGridUnseen where the frame doesn't reach. `out` has room for grid.Cells().
void LayOnGrid(
    const TrackingGrid& grid,
    const std::vector<float>& directions,
    const GreyLayout& layout,
    const GreyFrame& frame,
    uint8_t* out
);
