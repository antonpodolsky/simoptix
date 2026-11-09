// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// fake_layer_game: a fake game that loads the real OpenXR layer's DLL as the OpenXR loader does in LMU, for
// the local files test (vr_mirror\e2e\local_files\). build.bat names it bin\fake_layer_game\Le Mans
// Ultimate.exe, because the layer only wakes up in a program of that name. It loads the DLL, which notes the
// load in the layer's log, and with --handshake makes the loader's first call, which starts the log's
// writing. It needs no OpenXR runtime or headset, and makes no instance.
//
// Usage: "Le Mans Ultimate.exe" <the layer's DLL> [--handshake]
// Exit code 0 when the DLL loaded and, with --handshake, accepted the handshake.
#include <windows.h>

#include <cstdio>
#include <cwchar>

#include "openxr/openxr.h"
#include "openxr/openxr_loader_negotiation.h"

namespace {

    constexpr const char* kLayerName = "XR_APILAYER_SIMOPTIX_frame_source";
    constexpr DWORD kLogWriteMs = 500;  // time for the layer's own thread to write its log before the exit

    bool Handshake(HMODULE layer) {
        const auto negotiate = reinterpret_cast<PFN_xrNegotiateLoaderApiLayerInterface>(
            GetProcAddress(layer, "xrNegotiateLoaderApiLayerInterface")
        );

        if (!negotiate) {
            printf("the layer has no xrNegotiateLoaderApiLayerInterface\n");
            return false;
        }

        XrNegotiateLoaderInfo info = {};
        info.structType = XR_LOADER_INTERFACE_STRUCT_LOADER_INFO;
        info.structVersion = XR_LOADER_INFO_STRUCT_VERSION;
        info.structSize = sizeof(info);
        info.minInterfaceVersion = 1;
        info.maxInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
        info.minApiVersion = XR_MAKE_VERSION(1, 0, 0);
        info.maxApiVersion = XR_CURRENT_API_VERSION;

        XrNegotiateApiLayerRequest request = {};
        request.structType = XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST;
        request.structVersion = XR_API_LAYER_INFO_STRUCT_VERSION;
        request.structSize = sizeof(request);

        const XrResult r = negotiate(&info, kLayerName, &request);

        if (XR_FAILED(r)) {
            printf("the layer refused the handshake: %d\n", static_cast<int>(r));
            return false;
        }

        return true;
    }

}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        printf("usage: \"Le Mans Ultimate.exe\" <the layer's DLL> [--handshake]\n");
        return 2;
    }

    const bool handshake = argc > 2 && wcscmp(argv[2], L"--handshake") == 0;
    const HMODULE layer = LoadLibraryW(argv[1]);

    if (!layer) {
        printf("the layer's DLL didn't load: %lu\n", GetLastError());
        return 1;
    }

    if (handshake && !Handshake(layer)) {
        return 1;
    }

    Sleep(kLogWriteMs);
    return 0;
}
