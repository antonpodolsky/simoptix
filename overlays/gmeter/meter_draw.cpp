// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "meter_draw.h"

#include <graphics/vec3.h>
#include <graphics/vec4.h>
#include <obs-module.h>

#include <algorithm>
#include <cmath>

namespace {

    constexpr float kPi = 3.14159265f;
    constexpr float kFeather = 1.0f;  // soft edge width (source pixels)

    int Segments(float r, int lo, int hi) {
        return std::max(lo, std::min(hi, int(2 * kPi * r / 4)));
    }

    uint32_t Pack(float r, float g, float b, float a) {
        auto byte = [](float v) { return uint32_t(std::max(0.0f, std::min(1.0f, v)) * 255.0f + 0.5f); };
        return byte(a) << 24 | byte(b) << 16 | byte(g) << 8 | byte(r);
    }

}

void AddTri(
    std::vector<float>& v,
    float x0,
    float y0,
    float a0,
    float x1,
    float y1,
    float a1,
    float x2,
    float y2,
    float a2
) {
    const float t[] = {x0, y0, a0, x1, y1, a1, x2, y2, a2};
    v.insert(v.end(), t, t + 9);
}

void AddQuadA(
    std::vector<float>& v,
    float x0,
    float y0,
    float a0,
    float x1,
    float y1,
    float a1,
    float x2,
    float y2,
    float a2,
    float x3,
    float y3,
    float a3
) {
    AddTri(v, x0, y0, a0, x1, y1, a1, x2, y2, a2);
    AddTri(v, x0, y0, a0, x2, y2, a2, x3, y3, a3);
}

void AddTaper(std::vector<float>& v, float x0, float y0, float h0, float x1, float y1, float h1) {
    float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-3f) {
        return;
    }
    float nx = -dy / len, ny = dx / len;
    AddQuadA(
        v,
        x0 + nx * h0,
        y0 + ny * h0,
        1,
        x1 + nx * h1,
        y1 + ny * h1,
        1,
        x1 - nx * h1,
        y1 - ny * h1,
        1,
        x0 - nx * h0,
        y0 - ny * h0,
        1
    );

    for (float side : {1.0f, -1.0f}) {
        float sx = nx * side, sy = ny * side;
        AddQuadA(
            v,
            x0 + sx * h0,
            y0 + sy * h0,
            1,
            x1 + sx * h1,
            y1 + sy * h1,
            1,
            x1 + sx * (h1 + kFeather),
            y1 + sy * (h1 + kFeather),
            0,
            x0 + sx * (h0 + kFeather),
            y0 + sy * (h0 + kFeather),
            0
        );
    }
}

void AddArc(std::vector<float>& v, float cx, float cy, float r, float w, float a0, float a1) {
    const float t0 = a0 * kPi / 180, t1 = a1 * kPi / 180, span = t1 - t0;
    const float h = w * 0.5f;
    const int n = std::max(2, int(std::fabs(span) * r / 4));  // a segment every 4 px
    const float rad[4] = {r - h - kFeather, r - h, r + h, r + h + kFeather};
    const float alpha[4] = {0, 1, 1, 0};

    // Clockwise from up on screen (y down): angle t is at (sin t, -cos t).
    for (int i = 0; i < n; i++) {
        const float ta = t0 + span * i / n, tb = t0 + span * (i + 1) / n;
        const float sa = std::sin(ta), ca = std::cos(ta), sb = std::sin(tb), cb = std::cos(tb);
        for (int b = 0; b < 3; b++) {
            const float ra = std::max(0.0f, rad[b]), rb = rad[b + 1];
            AddQuadA(
                v,
                cx + ra * sa,
                cy - ra * ca,
                alpha[b],
                cx + rb * sa,
                cy - rb * ca,
                alpha[b + 1],
                cx + rb * sb,
                cy - rb * cb,
                alpha[b + 1],
                cx + ra * sb,
                cy - ra * cb,
                alpha[b]
            );
        }
    }

    // Round ends: a half disc beyond each end, its flat side on the band's end.
    const float dir = span >= 0 ? 1.0f : -1.0f;
    for (int end = 0; end < 2; end++) {
        const float t = end ? t1 : t0, out = end ? dir : -dir;
        const float px = cx + r * std::sin(t), py = cy - r * std::cos(t);
        const float ux = std::sin(t), uy = -std::cos(t);  // outward from the centre

        // along the circle, away from the band
        const float tx = out * std::cos(t), ty = out * std::sin(t);
        const int m = std::max(6, Segments(h, 8, 32) / 2);
        const float core = std::max(0.0f, h - kFeather * 0.5f), edge = h + kFeather * 0.5f;
        for (int i = 0; i < m; i++) {
            const float p0 = kPi * (float(i) / m - 0.5f), p1 = kPi * (float(i + 1) / m - 0.5f);
            const float d0x = tx * std::cos(p0) + ux * std::sin(p0),
                        d0y = ty * std::cos(p0) + uy * std::sin(p0);

            const float d1x = tx * std::cos(p1) + ux * std::sin(p1),
                        d1y = ty * std::cos(p1) + uy * std::sin(p1);

            AddTri(v, px, py, 1, px + core * d0x, py + core * d0y, 1, px + core * d1x, py + core * d1y, 1);
            AddQuadA(
                v,
                px + core * d0x,
                py + core * d0y,
                1,
                px + edge * d0x,
                py + edge * d0y,
                0,
                px + edge * d1x,
                py + edge * d1y,
                0,
                px + core * d1x,
                py + core * d1y,
                1
            );
        }
    }
}

void AddDisc(std::vector<float>& v, float cx, float cy, float r) {
    const int n = Segments(r, 16, 128);
    const float core = std::max(0.0f, r - kFeather * 0.5f), edge = r + kFeather * 0.5f;
    for (int i = 0; i < n; i++) {
        const float c0 = std::cos(2 * kPi * i / n), s0 = std::sin(2 * kPi * i / n);
        const float c1 = std::cos(2 * kPi * (i + 1) / n), s1 = std::sin(2 * kPi * (i + 1) / n);
        AddTri(v, cx, cy, 1, cx + core * c0, cy + core * s0, 1, cx + core * c1, cy + core * s1, 1);
        AddQuadA(
            v,
            cx + core * c0,
            cy + core * s0,
            1,
            cx + edge * c0,
            cy + edge * s0,
            0,
            cx + edge * c1,
            cy + edge * s1,
            0,
            cx + core * c1,
            cy + core * s1,
            1
        );
    }
}

void AddBlurredDisc(std::vector<float>& v, float cx, float cy, float r, float sigma) {
    // Rings of linearly interpolated alpha along the blurred edge's profile, 0.5 erfc((d - r) / (sigma
    // sqrt 2)) at distance d from the centre (the straight-edge profile; close to the round one while r
    // is over 2 sigma).
    const float outer = r + 3 * sigma;
    const int rings = 16, n = Segments(outer, 16, 128);
    auto alpha = [&](float d) { return 0.5f * std::erfc((d - r) / (std::max(1e-3f, sigma) * 1.41421356f)); };

    for (int k = 0; k < rings; k++) {
        const float ra = outer * k / rings, rb = outer * (k + 1) / rings, aa = alpha(ra), ab = alpha(rb);

        for (int i = 0; i < n; i++) {
            const float c0 = std::cos(2 * kPi * i / n), s0 = std::sin(2 * kPi * i / n);
            const float c1 = std::cos(2 * kPi * (i + 1) / n), s1 = std::sin(2 * kPi * (i + 1) / n);

            if (k == 0) {
                AddTri(v, cx, cy, aa, cx + rb * c0, cy + rb * s0, ab, cx + rb * c1, cy + rb * s1, ab);
            } else {
                AddQuadA(
                    v,
                    cx + ra * c0,
                    cy + ra * s0,
                    aa,
                    cx + rb * c0,
                    cy + rb * s0,
                    ab,
                    cx + rb * c1,
                    cy + rb * s1,
                    ab,
                    cx + ra * c1,
                    cy + ra * s1,
                    aa
                );
            }
        }
    }
}

void MeterBatch::Add(const std::vector<float>& v, float r, float g, float b, float a) {
    for (size_t i = 0; i + 2 < v.size(); i += 3) {
        frame_.push_back({v[i], v[i + 1], Pack(r, g, b, a * v[i + 2])});
    }
}

// OBS's solid effect, "SolidColored": the vertex colours. Not OBS's gs_render_start/stop: with
// per-vertex colours that path left OBS's next draw crashing (encoder colour conversion, in
// LoadVertexBufferData).
void MeterBatch::Submit() {
    const size_t n = std::min(frame_.size() / 3 * 3, kMaxVertices);
    if (n == 0) {
        return;
    }
    if (!vb_) {
        gs_vb_data* vbd = gs_vbdata_create();
        vbd->num = kMaxVertices;
        vbd->points = static_cast<vec3*>(bzalloc(sizeof(vec3) * kMaxVertices));
        vbd->colors = static_cast<uint32_t*>(bzalloc(sizeof(uint32_t) * kMaxVertices));
        vb_ = gs_vertexbuffer_create(vbd, GS_DYNAMIC);
        if (!vb_) {
            return;
        }
    }

    gs_vb_data* d = gs_vertexbuffer_get_data(vb_);
    for (size_t i = 0; i < n; i++) {
        vec3_set(&d->points[i], frame_[i].x, frame_[i].y, 0.0f);
        d->colors[i] = frame_[i].color;
    }

    for (size_t i = n; i < kMaxVertices && i < n + 3; i++) {
        d->colors[i] = 0;
    }
    gs_vertexbuffer_flush(vb_);
    gs_effect_t* solid = obs_get_base_effect(OBS_EFFECT_SOLID);
    vec4 white;
    vec4_set(&white, 1, 1, 1, 1);
    gs_effect_set_vec4(gs_effect_get_param_by_name(solid, "color"), &white);
    gs_load_vertexbuffer(vb_);
    gs_load_indexbuffer(nullptr);
    while (gs_effect_loop(solid, "SolidColored")) {
        gs_draw(GS_TRIS, 0, uint32_t(n));
    }
    gs_load_vertexbuffer(nullptr);  // leave nothing of ours bound for OBS's next draw
}

void MeterBatch::Destroy() {
    if (vb_) {
        gs_vertexbuffer_destroy(vb_);  // frees its vertex data too
    }
    vb_ = nullptr;
}
