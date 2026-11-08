// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Frame layout: a fixed-size summary of what the game hands the headset at the end of each frame
// (which pictures, from which swapchain, which part of each), so the layer can log it once and again
// only when it changes. Also the description of a swapchain the game made.
// Plain data and text formatting only: it reads the OpenXR structs the game passed and calls nothing.
// It doesn't know about the loader, the log file or the layer's other calls.
#pragma once

#include <cstddef>
#include <cstdint>

#define XR_NO_PROTOTYPES
#include "openxr/openxr.h"

constexpr uint32_t kMaxLayoutLayers = 4;  // layers summarised per frame; LMU sends one projection layer
constexpr uint32_t kMaxLayoutViews = 4;   // views per projection layer; stereo is 2, quad views 4

// One swapchain as the game created it.
struct SwapchainInfo {
    uint64_t handle = 0;             // the XrSwapchain's value, to match frames to it
    uint32_t width = 0, height = 0;  // pixels
    int64_t format = 0;              // the graphics API's format: a DXGI_FORMAT for D3D11 and D3D12
    uint32_t arraySize = 0, mipCount = 0, sampleCount = 0, faceCount = 0;
    XrSwapchainUsageFlags usage = 0;
};

// One view of a projection layer (one eye), or a quad layer's picture.
struct LayoutView {
    uint64_t swapchain = 0;                       // the XrSwapchain's value
    int32_t x = 0, y = 0, width = 0, height = 0;  // the part of the image used, in pixels
    uint32_t arrayIndex = 0;                      // the slice of an array swapchain
    bool hasDepth = false;  // a depth picture is attached (XR_KHR_composition_layer_depth)
    XrFovf fov = {};        // radians, left and down negative; not compared, it may change per frame
};

struct LayoutLayer {
    XrStructureType type = XR_TYPE_UNKNOWN;
    XrCompositionLayerFlags flags = 0;
    uint32_t viewCount = 0;  // as passed; only the first kMaxLayoutViews are kept
    LayoutView views[kMaxLayoutViews];
};

struct OpenXrFrameLayout {
    XrEnvironmentBlendMode blendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    uint32_t layerCount = 0;  // as passed; only the first kMaxLayoutLayers are kept
    LayoutLayer layers[kMaxLayoutLayers];
};

SwapchainInfo DescribeSwapchain(uint64_t handle, const XrSwapchainCreateInfo& info);
OpenXrFrameLayout SummarizeFrame(const XrFrameEndInfo& info);

// Same layers, swapchains and image rects. The field of view is left out.
bool SameLayout(const OpenXrFrameLayout& a, const OpenXrFrameLayout& b);

// One line of text each, without a trailing newline; they return `out`.
const char* FormatSwapchain(const SwapchainInfo& s, char* out, size_t size);
const char* FormatLayer(const LayoutLayer& layer, uint32_t index, char* out, size_t size);
// The swapchain by its handle; its size and format are in the line logged when it was made.
const char* FormatView(const LayoutView& view, uint32_t index, char* out, size_t size);

// "R8G8B8A8_UNORM_SRGB" for the DXGI formats games use for colour, else nullptr.
const char* DxgiFormatName(int64_t format);
// "projection", "quad", ... for composition layer types; "D3D11" for graphics bindings.
const char* XrTypeName(XrStructureType type);
