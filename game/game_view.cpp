// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "game_view.h"

#include <cmath>

namespace {

    // Row-major rotation matrix of a unit quaternion.
    void RotationOf(const XrQuaternionf& q, float m[3][3]) {
        const float x = q.x, y = q.y, z = q.z, w = q.w;

        m[0][0] = 1 - 2 * (y * y + z * z);
        m[0][1] = 2 * (x * y - z * w);
        m[0][2] = 2 * (x * z + y * w);
        m[1][0] = 2 * (x * y + z * w);
        m[1][1] = 1 - 2 * (x * x + z * z);
        m[1][2] = 2 * (y * z - x * w);
        m[2][0] = 2 * (x * z - y * w);
        m[2][1] = 2 * (y * z + x * w);
        m[2][2] = 1 - 2 * (x * x + y * y);
    }

}

float HeadYaw(const XrQuaternionf& head) {
    const float x = head.x, y = head.y, z = head.z, w = head.w;
    return std::atan2(2 * (x * z + y * w), 1 - 2 * (x * x + y * y));
}

GridView ViewOfEye(const XrQuaternionf& eye, const XrFovf& fov, float chartYaw) {
    float roomFromEye[3][3];
    RotationOf(eye, roomFromEye);

    // The chart's axes are the room's turned by chartYaw about +y. Going from the room to the chart turns
    // back by it: the transpose of that turn.
    const float c = std::cos(chartYaw), s = std::sin(chartYaw);
    const float chartFromRoom[3][3] = {
        {c, 0, -s},
        {0, 1, 0},
        {s, 0, c},
    };

    GridView view = {};

    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            view.chartFromEye[row][col] = chartFromRoom[row][0] * roomFromEye[0][col]
                                          + chartFromRoom[row][1] * roomFromEye[1][col]
                                          + chartFromRoom[row][2] * roomFromEye[2][col];
        }
    }

    view.tanLeft = std::tan(fov.angleLeft);
    view.tanRight = std::tan(fov.angleRight);
    view.tanUp = std::tan(fov.angleUp);
    view.tanDown = std::tan(fov.angleDown);
    return view;
}
