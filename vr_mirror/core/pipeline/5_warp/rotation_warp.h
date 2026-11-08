// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// RotationWarp: the warp as a pure rotation. Each output ray is turned from the view into the eye
// camera the picture was drawn from (through the eye's cant), and looked up in the eye image: a
// re-projection for a change of head orientation only (head position is ignored; the scene is far
// enough away). Stateless. Knows nothing about smoothing, frames or timing.
#pragma once

#include "pipeline/5_warp/warp.h"

class RotationWarp : public Warp {
public:
    WarpParams Params(const Quat& shown, const Quat& view, const ViewGeometry& g) const override;
};
