// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "tracking_grid.h"

#include <algorithm>
#include <cmath>

std::vector<float> GridDirections(const TrackingGrid& grid) {
    std::vector<float> directions;
    directions.reserve(static_cast<size_t>(grid.Cells()) * 3);
    const float degree = std::acos(-1.0f) / 180;

    for (int row = 0; row < grid.rows; row++) {
        const float pitch = (grid.topDeg - (row + 0.5f) * grid.cellDeg) * degree;

        for (int column = 0; column < grid.columns; column++) {
            const float yaw = (grid.leftDeg + (column + 0.5f) * grid.cellDeg) * degree;
            directions.push_back(std::cos(pitch) * std::sin(yaw));
            directions.push_back(std::sin(pitch));
            directions.push_back(-std::cos(pitch) * std::cos(yaw));
        }
    }

    return directions;
}

void LayOnGrid(
    const TrackingGrid& grid,
    const std::vector<float>& directions,
    const GreyLayout& layout,
    const GreyFrame& frame,
    uint8_t* out
) {
    const EyeInfo& eye = layout.eye;
    float toEye[3][3];
    ToMatrix(Conj(Mul(frame.pose, eye.hmdToEye)), toEye);
    const float* area = layout.area;

    for (int cellIndex = 0; cellIndex < grid.Cells(); cellIndex++) {
        const float* d = directions.data() + static_cast<size_t>(cellIndex) * 3;
        const float dx = d[0], dy = d[1], dz = d[2];
        const float ex = toEye[0][0] * dx + toEye[0][1] * dy + toEye[0][2] * dz;
        const float ey = toEye[1][0] * dx + toEye[1][1] * dy + toEye[1][2] * dz;
        const float ez = toEye[2][0] * dx + toEye[2][1] * dy + toEye[2][2] * dz;
        uint8_t& cell = out[cellIndex];
        cell = kGridUnseen;

        if (ez > -1e-3f) {
            continue;
        }

        const float u = (ex / -ez + eye.leftTan) / (eye.leftTan + eye.rightTan);
        const float v = (eye.upTan - ey / -ez) / (eye.upTan + eye.downTan);
        const float px = (u - area[0]) / (area[2] - area[0]) * layout.width - 0.5f;
        const float py = (v - area[1]) / (area[3] - area[1]) * layout.height - 0.5f;

        if (px < 0 || py < 0 || px >= layout.width - 1 || py >= layout.height - 1) {
            continue;
        }

        const int x = static_cast<int>(px), y = static_cast<int>(py);
        const float fx = px - x, fy = py - y;
        const float* top = frame.values.data() + static_cast<size_t>(y) * layout.width + x;
        const float* bottom = top + layout.width;
        const float value = (top[0] * (1 - fx) + top[1] * fx) * (1 - fy)
                            + (bottom[0] * (1 - fx) + bottom[1] * fx) * fy;
        cell = static_cast<uint8_t>(std::clamp(value * 254.0f, 0.0f, 254.0f));
    }
}
