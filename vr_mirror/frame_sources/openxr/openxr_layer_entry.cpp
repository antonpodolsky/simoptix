// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The layer's entry: what the OpenXR loader calls. The loader loads every registered layer into every
// OpenXR program (the game, Pimax Play's home, SteamVR tools). This file negotiates with the loader,
// joins the chain of layers when a program makes its OpenXR instance, and answers which function to call
// for each OpenXR call.
//
// Only in Le Mans Ultimate (the program "Le Mans Ultimate.exe", whose OpenXR application name is
// "LeMansUltimate") does it hand out the calls openxr_layer_calls.cpp looks at, and write a log. Everywhere
// else it hands out the next layer's or the runtime's own functions, so after the instance is made the
// layer is out of the way: nothing of it runs.
// In the game, the one thing it changes in a call: the game's instance also gets the runtime's time
// conversion extension, if the game didn't ask for it and the runtime offers it (the frame source needs
// it to put the frames' times on the clock VR Mirror uses).
// It doesn't know what the calls it hands out do.
#include <windows.h>

#include <atomic>
#include <cstring>
#include <vector>

#define XR_NO_PROTOTYPES
#include "openxr/openxr.h"
#include "openxr/openxr_loader_negotiation.h"
#include "openxr_d3d11.h"
#include "openxr_layer_calls.h"
#include "openxr_layer_log.h"
#include "shared/log.h"
#include "simoptix_version.h"  // written into obj\ by shared\build\version.bat

namespace {

    constexpr const char* kLayerName = "XR_APILAYER_SIMOPTIX_frame_source";  // as in the manifest
    constexpr const wchar_t* kGameExe = L"Le Mans Ultimate.exe";
    constexpr const char* kGameApplicationName = "LeMansUltimate";  // what LMU tells OpenXR it is

    // Set once in xrNegotiateLoaderApiLayerInterface: this process is the game's.
    std::atomic<bool> g_inGame{false};
    // Set when the game's instance is made: hand out the layer's own calls.
    std::atomic<bool> g_active{false};
    // The next layer's or the runtime's xrGetInstanceProcAddr, from the newest instance.
    std::atomic<PFN_xrGetInstanceProcAddr> g_nextGetInstanceProcAddr{nullptr};

    bool ProcessIsGame() {
        wchar_t path[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) {
            return false;
        }
        const wchar_t* name = wcsrchr(path, L'\\');
        name = name ? name + 1 : path;
        return _wcsicmp(name, kGameExe) == 0;
    }

    XrResult XRAPI_CALL
    LayerGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function) {
        PFN_xrGetInstanceProcAddr next = g_nextGetInstanceProcAddr.load();
        if (!next) {
            if (function) {
                *function = nullptr;
            }
            return XR_ERROR_HANDLE_INVALID;
        }

        XrResult r = next(instance, name, function);
        if (XR_FAILED(r) || !g_active.load() || !name || !function || !*function) {
            return r;
        }
        if (strcmp(name, "xrGetInstanceProcAddr") == 0) {
            *function = reinterpret_cast<PFN_xrVoidFunction>(LayerGetInstanceProcAddr);
        } else if (PFN_xrVoidFunction own = LayerCall(name)) {
            *function = own;
        }

        return r;
    }

    // Whether the runtime (through the rest of the chain) offers instance extension `name`.
    bool RuntimeOffers(PFN_xrGetInstanceProcAddr next, const char* name) {
        PFN_xrEnumerateInstanceExtensionProperties enumerate = nullptr;
        if (XR_FAILED(next(
                XR_NULL_HANDLE,
                "xrEnumerateInstanceExtensionProperties",
                reinterpret_cast<PFN_xrVoidFunction*>(&enumerate)
            ))
            || !enumerate) {
            return false;
        }

        uint32_t count = 0;
        if (XR_FAILED(enumerate(nullptr, 0, &count, nullptr)) || count == 0) {
            return false;
        }
        std::vector<XrExtensionProperties> props(count, {XR_TYPE_EXTENSION_PROPERTIES});
        if (XR_FAILED(enumerate(nullptr, count, &count, props.data()))) {
            return false;
        }
        for (uint32_t i = 0; i < count; i++) {
            if (strcmp(props[i].extensionName, name) == 0) {
                return true;
            }
        }

        return false;
    }

    bool Asks(const XrInstanceCreateInfo& info, const char* name) {
        for (uint32_t i = 0; i < info.enabledExtensionCount; i++) {
            if (strcmp(info.enabledExtensionNames[i], name) == 0) {
                return true;
            }
        }

        return false;
    }

    const char* AddTimeConversion(
        const XrInstanceCreateInfo* info,
        const XrApiLayerNextInfo* mine,
        XrInstanceCreateInfo* passed,
        std::vector<const char*>* extensions
    ) {
        const char* asked = "";
        try {
            if (g_inGame && strcmp(info->applicationInfo.applicationName, kGameApplicationName) == 0
                && !Asks(*info, kXrTimeConversionExtension)) {
                if (RuntimeOffers(mine->nextGetInstanceProcAddr, kXrTimeConversionExtension)) {
                    extensions->assign(
                        info->enabledExtensionNames,
                        info->enabledExtensionNames + info->enabledExtensionCount
                    );

                    extensions->push_back(kXrTimeConversionExtension);
                    passed->enabledExtensionCount = static_cast<uint32_t>(extensions->size());
                    passed->enabledExtensionNames = extensions->data();
                    asked = "added";
                } else {
                    asked = "not offered by the runtime";
                }
            }
        } catch (...) {
            *passed = *info;
            asked = "";
        }

        return asked;
    }

    void StartInGame(
        const XrInstanceCreateInfo* info,
        const XrApiLayerNextInfo* mine,
        const char* asked,
        XrInstance instance,
        const XrInstanceCreateInfo& passed
    ) {
        try {
            const char* app = info->applicationInfo.applicationName;
            CoreLog("instance made by \"%s\" (engine \"%s\")", app, info->applicationInfo.engineName);
            if (*asked) {
                CoreLog("%s: %s", kXrTimeConversionExtension, asked);
            }
            for (const XrApiLayerNextInfo* n = mine->next; n; n = n->next) {
                CoreLog("next layer: %s", n->layerName);
            }

            if (strcmp(app, kGameApplicationName) != 0) {
                CoreLog("not \"%s\": passing every call through", kGameApplicationName);
                return;
            }

            StartCalls(instance, mine->nextGetInstanceProcAddr, passed);
            g_active = true;
        } catch (...) {
            g_active = false;
        }
    }

    XrResult XRAPI_CALL LayerCreateApiLayerInstance(
        const XrInstanceCreateInfo* info,
        const XrApiLayerCreateInfo* layerInfo,
        XrInstance* instance
    ) {
        if (!info || !layerInfo || !layerInfo->nextInfo || !instance) {
            return XR_ERROR_INITIALIZATION_FAILED;
        }
        const XrApiLayerNextInfo* mine = layerInfo->nextInfo;
        if (strcmp(mine->layerName, kLayerName) != 0) {
            if (g_inGame) {
                CoreLog("the loader passed layer %s's place in the chain, not ours", mine->layerName);
            }
            return XR_ERROR_INITIALIZATION_FAILED;
        }

        // The rest of the chain, as the next layer (or the loader, for the runtime) must see it.
        XrApiLayerCreateInfo rest = *layerInfo;
        rest.nextInfo = mine->next;

        // The game's instance, with the time conversion extension added if it lacks it and the runtime has
        // it.
        XrInstanceCreateInfo passed = *info;
        std::vector<const char*> extensions;
        const char* asked = AddTimeConversion(info, mine, &passed, &extensions);

        XrResult r = mine->nextCreateApiLayerInstance(&passed, &rest, instance);
        if (XR_FAILED(r) && passed.enabledExtensionNames != info->enabledExtensionNames) {
            if (g_inGame) {
                CoreLog(
                    "instance with %s not created (%d): trying the game's own",
                    kXrTimeConversionExtension,
                    static_cast<int>(r)
                );
            }

            passed = *info;
            asked = "";
            r = mine->nextCreateApiLayerInstance(&passed, &rest, instance);
        }

        if (XR_FAILED(r)) {
            if (g_inGame) {
                CoreLog("instance not created: %d", static_cast<int>(r));
            }
            return r;
        }

        g_nextGetInstanceProcAddr = mine->nextGetInstanceProcAddr;
        if (!g_inGame) {
            return r;  // any other program: pass everything through
        }

        StartInGame(info, mine, asked, *instance, passed);
        return r;
    }

}

// Windows calls this when it loads the DLL, before the OpenXR loader calls anything in it.
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH && ProcessIsGame()) {
        NoteDllLoaded();
    }
    return TRUE;
}

extern "C" XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loaderInfo,
    const char* layerName,
    XrNegotiateApiLayerRequest* request
) {
    // In the game the log starts first, so a refused handshake is written down too.
    try {
        if (!g_inGame && ProcessIsGame()) {
            g_inGame = true;
            if (StartLayerLog()) {
                CoreLog(
                    "%s %s loaded into %ls, process %lu (layer built %s %s)",
                    kLayerName,
                    SIMOPTIX_VERSION,
                    kGameExe,
                    GetCurrentProcessId(),
                    __DATE__,
                    __TIME__
                );
            }
        }

        if (g_inGame && loaderInfo) {
            CoreLog(
                "loader offers layer interface %u-%u, OpenXR %u.%u.%u-%u.%u.%u; asks for \"%s\"",
                loaderInfo->minInterfaceVersion,
                loaderInfo->maxInterfaceVersion,
                XR_VERSION_MAJOR(loaderInfo->minApiVersion),
                XR_VERSION_MINOR(loaderInfo->minApiVersion),
                XR_VERSION_PATCH(loaderInfo->minApiVersion),
                XR_VERSION_MAJOR(loaderInfo->maxApiVersion),
                XR_VERSION_MINOR(loaderInfo->maxApiVersion),
                XR_VERSION_PATCH(loaderInfo->maxApiVersion),
                layerName ? layerName : "(no name)"
            );
        }
    } catch (...) {
    }

    const char* refused = nullptr;
    if (!loaderInfo || !request) {
        refused = "no loader info or request";
    } else if (
        loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO
        || loaderInfo->structVersion != XR_LOADER_INFO_STRUCT_VERSION
        || loaderInfo->structSize != sizeof(XrNegotiateLoaderInfo)
    ) {
        refused = "the loader info's type, version or size";
    } else if (
        request->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST
        || request->structVersion != XR_API_LAYER_INFO_STRUCT_VERSION
        || request->structSize != sizeof(XrNegotiateApiLayerRequest)
    ) {
        refused = "the request's type, version or size";
    } else if (
        loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION
        || loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION
    ) {
        refused = "the layer interface version";
    } else if (layerName && strcmp(layerName, kLayerName) != 0) {
        refused = "the layer name";
    }

    if (refused) {
        if (g_inGame) {
            CoreLog("handshake refused: %s doesn't match", refused);
        }
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    request->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    request->layerApiVersion = XR_CURRENT_API_VERSION;
    request->getInstanceProcAddr = LayerGetInstanceProcAddr;
    request->createApiLayerInstance = LayerCreateApiLayerInstance;
    return XR_SUCCESS;
}
