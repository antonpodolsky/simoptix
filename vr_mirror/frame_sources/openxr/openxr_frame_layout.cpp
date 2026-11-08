// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "openxr_frame_layout.h"

#include <cstdio>

namespace {

    constexpr float kDegPerRad = 57.29578f;

    void CopyView(const XrSwapchainSubImage& sub, LayoutView& v) {
        v.swapchain = reinterpret_cast<uint64_t>(sub.swapchain);
        v.x = sub.imageRect.offset.x;
        v.y = sub.imageRect.offset.y;
        v.width = sub.imageRect.extent.width;
        v.height = sub.imageRect.extent.height;
        v.arrayIndex = sub.imageArrayIndex;
    }

    bool HasDepth(const void* next) {
        for (auto* s = static_cast<const XrBaseInStructure*>(next); s; s = s->next) {
            if (s->type == XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR) {
                return true;
            }
        }

        return false;
    }

    bool SameView(const LayoutView& a, const LayoutView& b) {
        return a.swapchain == b.swapchain && a.x == b.x && a.y == b.y && a.width == b.width
               && a.height == b.height && a.arrayIndex == b.arrayIndex && a.hasDepth == b.hasDepth;
    }

}

SwapchainInfo DescribeSwapchain(uint64_t handle, const XrSwapchainCreateInfo& info) {
    SwapchainInfo s;
    s.handle = handle;
    s.width = info.width;
    s.height = info.height;
    s.format = info.format;
    s.arraySize = info.arraySize;
    s.mipCount = info.mipCount;
    s.sampleCount = info.sampleCount;
    s.faceCount = info.faceCount;
    s.usage = info.usageFlags;
    return s;
}

OpenXrFrameLayout SummarizeFrame(const XrFrameEndInfo& info) {
    OpenXrFrameLayout f;
    f.blendMode = info.environmentBlendMode;
    f.layerCount = info.layerCount;

    for (uint32_t i = 0; i < info.layerCount && i < kMaxLayoutLayers; i++) {
        const XrCompositionLayerBaseHeader* h = info.layers ? info.layers[i] : nullptr;
        if (!h) {
            continue;
        }

        LayoutLayer& l = f.layers[i];
        l.type = h->type;
        l.flags = h->layerFlags;

        if (h->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
            auto* p = reinterpret_cast<const XrCompositionLayerProjection*>(h);
            l.viewCount = p->viewCount;
            for (uint32_t v = 0; v < p->viewCount && v < kMaxLayoutViews && p->views; v++) {
                CopyView(p->views[v].subImage, l.views[v]);
                l.views[v].hasDepth = HasDepth(p->views[v].next);
                l.views[v].fov = p->views[v].fov;
            }
        } else if (h->type == XR_TYPE_COMPOSITION_LAYER_QUAD) {
            auto* q = reinterpret_cast<const XrCompositionLayerQuad*>(h);
            l.viewCount = 1;
            CopyView(q->subImage, l.views[0]);
        }
    }

    return f;
}

bool SameLayout(const OpenXrFrameLayout& a, const OpenXrFrameLayout& b) {
    if (a.blendMode != b.blendMode || a.layerCount != b.layerCount) {
        return false;
    }
    for (uint32_t i = 0; i < a.layerCount && i < kMaxLayoutLayers; i++) {
        const LayoutLayer& x = a.layers[i];
        const LayoutLayer& y = b.layers[i];
        if (x.type != y.type || x.flags != y.flags || x.viewCount != y.viewCount) {
            return false;
        }
        for (uint32_t v = 0; v < x.viewCount && v < kMaxLayoutViews; v++) {
            if (!SameView(x.views[v], y.views[v])) {
                return false;
            }
        }
    }

    return true;
}

const char* FormatSwapchain(const SwapchainInfo& s, char* out, size_t size) {
    const char* name = DxgiFormatName(s.format);
    char format[48];
    if (name) {
        snprintf(format, sizeof(format), "%s (%lld)", name, static_cast<long long>(s.format));
    } else {
        snprintf(format, sizeof(format), "%lld", static_cast<long long>(s.format));
    }

    snprintf(
        out,
        size,
        "swapchain %llx: %ux%u, format %s, array %u, mips %u, samples %u, faces %u, usage 0x%llx",
        static_cast<unsigned long long>(s.handle),
        s.width,
        s.height,
        format,
        s.arraySize,
        s.mipCount,
        s.sampleCount,
        s.faceCount,
        static_cast<unsigned long long>(s.usage)
    );

    return out;
}

const char* FormatLayer(const LayoutLayer& layer, uint32_t index, char* out, size_t size) {
    const char* name = XrTypeName(layer.type);
    char type[32];
    if (name) {
        snprintf(type, sizeof(type), "%s", name);
    } else {
        snprintf(type, sizeof(type), "type %d", static_cast<int>(layer.type));
    }

    snprintf(
        out,
        size,
        "layer %u: %s, flags 0x%llx, %u view(s)",
        index,
        type,
        static_cast<unsigned long long>(layer.flags),
        layer.viewCount
    );

    return out;
}

const char* FormatView(const LayoutView& view, uint32_t index, char* out, size_t size) {
    snprintf(
        out,
        size,
        "  view %u: swapchain %llx, rect %d,%d %dx%d, slice %u, depth %s, fov left %.2f right %.2f up "
        "%.2f "
        "down %.2f deg",
        index,
        static_cast<unsigned long long>(view.swapchain),
        view.x,
        view.y,
        view.width,
        view.height,
        view.arrayIndex,
        view.hasDepth ? "yes" : "no",
        view.fov.angleLeft * kDegPerRad,
        view.fov.angleRight * kDegPerRad,
        view.fov.angleUp * kDegPerRad,
        view.fov.angleDown * kDegPerRad
    );

    return out;
}

const char* DxgiFormatName(int64_t format) {
    switch (format) {
        case 2:
            return "R32G32B32A32_FLOAT";
        case 10:
            return "R16G16B16A16_FLOAT";
        case 24:
            return "R10G10B10A2_UNORM";
        case 26:
            return "R11G11B10_FLOAT";
        case 27:
            return "R8G8B8A8_TYPELESS";
        case 28:
            return "R8G8B8A8_UNORM";
        case 29:
            return "R8G8B8A8_UNORM_SRGB";
        case 40:
            return "D32_FLOAT";
        case 45:
            return "D24_UNORM_S8_UINT";
        case 55:
            return "D16_UNORM";
        case 87:
            return "B8G8R8A8_UNORM";
        case 90:
            return "B8G8R8A8_TYPELESS";
        case 91:
            return "B8G8R8A8_UNORM_SRGB";
        default:
            return nullptr;
    }
}

const char* XrTypeName(XrStructureType type) {
    switch (type) {
        case XR_TYPE_COMPOSITION_LAYER_PROJECTION:
            return "projection";
        case XR_TYPE_COMPOSITION_LAYER_QUAD:
            return "quad";
        case XR_TYPE_COMPOSITION_LAYER_CUBE_KHR:
            return "cube";
        case XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR:
            return "cylinder";
        case XR_TYPE_COMPOSITION_LAYER_EQUIRECT_KHR:
            return "equirect";
        case XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR:
            return "equirect2";
        case XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB:
            return "passthrough";
        case XR_TYPE_GRAPHICS_BINDING_D3D11_KHR:
            return "D3D11";
        case XR_TYPE_GRAPHICS_BINDING_D3D12_KHR:
            return "D3D12";
        case XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR:
            return "Vulkan";
        case XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR:
            return "OpenGL";
        default:
            return nullptr;
    }
}
