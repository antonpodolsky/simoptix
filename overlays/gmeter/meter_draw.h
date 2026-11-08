// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Meter drawing: anti-aliased shapes (bands, arcs, discs) as triangles in source pixels,
// batched per frame into one vertex buffer and drawn with OBS's solid-colour effect. Shapes are
// built as (x, y, alpha) triples into a std::vector<float>; MeterBatch::Add colours them, and
// Submit draws the whole frame in one call.
//
// Knows nothing about what a meter shows, telemetry, text or OBS sources: only libobs graphics.
// Graphics thread only (Submit and Destroy draw or free GPU resources).
#pragma once

#include <graphics/graphics.h>

#include <cstdint>
#include <vector>

// Triangles in source pixels. Vertices are (x, y, alpha) triples: shapes get a one-pixel soft edge
// that fades to transparent, so circles and the tail don't show stair steps (geometric anti-aliasing).
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
);

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
);

// A band from (x0, y0) with half width h0 to (x1, y1) with half width h1, with soft long edges.
void AddTaper(std::vector<float>& v, float x0, float y0, float h0, float x1, float y1, float h1);
// A band of width w along the circle of radius r around (cx, cy), from angle a0 to a1 (degrees,
// clockwise from straight up, as on screen), with soft edges and round ends that don't overlap the
// band (so a translucent arc has even alpha).
void AddArc(std::vector<float>& v, float cx, float cy, float r, float w, float a0, float a1);
void AddDisc(std::vector<float>& v, float cx, float cy, float r);
// A disc of radius r blurred by a Gaussian of `sigma` (as SVG's feGaussianBlur), for glows: alpha falls
// from about 1 inside to 0 at r + 3 sigma.
void AddBlurredDisc(std::vector<float>& v, float cx, float cy, float r, float sigma);

// Everything drawn in one frame, uploaded once. Add() in drawing order, then Submit().
class MeterBatch {
public:
    void Clear() { frame_.clear(); }
    // Adds the triangles in `v` in one colour, each vertex at its own alpha.
    void Add(const std::vector<float>& v, float r, float g, float b, float a);
    // Draws the frame in one call from the batch's own vertex buffer (created on the first frame, kept).
    void Submit();
    void Destroy();  // inside obs_enter_graphics, or on the graphics thread

private:
    struct Vertex {
        float x, y;
        uint32_t color;  // 0xAABBGGRR
    };

    static constexpr size_t kMaxVertices = 65536;  // the batch's own vertex buffer (about 1 MB)
    std::vector<Vertex> frame_;
    gs_vertbuffer_t* vb_ = nullptr;
};
