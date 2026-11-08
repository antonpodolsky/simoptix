// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The few Windows-specific OpenXR declarations the layer and its check program use: the D3D11 binding
// (XR_KHR_D3D11_enable) and the performance-counter time conversion
// (XR_KHR_win32_convert_performance_counter_time). They live in openxr_platform.h, which
// third_party\openxr doesn't carry; these have the same layout and values as the specification's.
// Declarations only.
#pragma once

#include <windows.h>
#include <d3d11.h>

#define XR_NO_PROTOTYPES
#include "openxr/openxr.h"

constexpr const char* kXrTimeConversionExtension = "XR_KHR_win32_convert_performance_counter_time";

// XrGraphicsBindingD3D11KHR
struct XrD3d11Binding {
    XrStructureType type;
    const void* next;
    ID3D11Device* device;
};

// XrSwapchainImageD3D11KHR
struct XrD3d11SwapchainImage {
    XrStructureType type;
    void* next;
    ID3D11Texture2D* texture;
};

// XrGraphicsRequirementsD3D11KHR
struct XrD3d11Requirements {
    XrStructureType type;
    void* next;
    LUID adapterLuid;
    D3D_FEATURE_LEVEL minFeatureLevel;
};

using PFN_XrGetD3d11Requirements = XrResult(XRAPI_PTR*)(XrInstance, XrSystemId, XrD3d11Requirements*);
// xrConvertWin32PerformanceCounterToTimeKHR, xrConvertTimeToWin32PerformanceCounterKHR
using PFN_XrCounterToTime = XrResult(XRAPI_PTR*)(XrInstance, const LARGE_INTEGER*, XrTime*);
using PFN_XrTimeToCounter = XrResult(XRAPI_PTR*)(XrInstance, XrTime, LARGE_INTEGER*);
