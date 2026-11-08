// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Where the runner reads the tool's window: the patches of each captured image, as bit numbers
// (ViewSample::noCode, Scenario::borderPatches). Layout only.
#pragma once

enum Patch {
    kCenter,
    kQuarterLeft,
    kQuarterRight,  // the view's direction and roll
    kLeft,
    kRight,
    kTop,
    kBottom,  // edge midpoints, right at the edge (black borders)
    kTopLeft,
    kTopRight,
    kBottomLeft,
    kBottomRight,  // corners, where black shows first in a tilt
    kPatches
};

constexpr int Bit(Patch p) {
    return 1 << p;
}
constexpr int kEdgePatches = Bit(kLeft) | Bit(kRight) | Bit(kTop) | Bit(kBottom) | Bit(kTopLeft)
                             | Bit(kTopRight) | Bit(kBottomLeft) | Bit(kBottomRight);
