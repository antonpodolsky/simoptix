// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "pipeline/5_warp/rotation_warp.h"

WarpParams RotationWarp::Params(const Quat& shown, const Quat& view, const ViewGeometry& g) const {
    WarpParams p = {};
    float rm[3][3];
    ViewToEye(shown, view, g, rm);  // re-projects the image from the pose it was rendered with
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            p.m[r][c] = rm[r][c];
        }
    }

    p.outTan[0] = g.outW;
    p.outTan[1] = g.outH;
    p.srcTan[0] = g.left;
    p.srcTan[1] = g.right;
    p.srcTan[2] = g.up;
    p.srcTan[3] = g.down;
    p.rect[2] = p.rect[3] = 1;                    // the eye fills the texture: u0, v0 = 0, u1, v1 = 1
    p.options[0] = g.encodedTwice ? 1.0f : 0.0f;  // a picture encoded twice: decode once
    return p;
}
