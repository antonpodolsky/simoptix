// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The layout check: compares the project's own declarations of the PVR client interface
// (pvr_interface.h) with Pimax's SDK headers, where those are installed. Everything is checked while
// compiling: each struct's size, alignment and field offsets and sizes, the function table's size and
// the place of each entry VR Mirror uses, each of those entries' return and parameter types as the
// x64 calling convention passes them (size, floating point or not, and for pointers the size of what
// they point to), and the constants' values. If it compiles, it matches; running it only says so.
//
// A standalone program: never part of the plugin, the mirror app or the e2e tests, which need none of
// Pimax's files. Knows nothing about sessions, mirrors or poses.

#include <cstddef>
#include <cstdio>
#include <string_view>
#include <type_traits>

#include "PVR_Interface.h"
#include "vr_mirror/frame_sources/pimax/pvr_interface.h"

namespace {

    // How the x64 calling convention sees a type: its size, whether it goes in a floating-point
    // register, and for a pointer the size of what it points to (0 for void).
    template <class T>
    struct Abi {
        static constexpr size_t size = sizeof(T);
        static constexpr bool floating = std::is_floating_point<T>::value;
        static constexpr size_t pointee = 0;
    };

    template <>
    struct Abi<void> {
        static constexpr size_t size = 0;
        static constexpr bool floating = false;
        static constexpr size_t pointee = 0;
    };

    template <class T>
    struct Abi<T*> {
        static constexpr size_t size = sizeof(T*);
        static constexpr bool floating = false;
        static constexpr size_t pointee = Abi<std::remove_cv_t<T>>::size;
    };

    template <class A, class B>
    constexpr bool SameAbi() {
        return Abi<A>::size == Abi<B>::size && Abi<A>::floating == Abi<B>::floating
               && Abi<A>::pointee == Abi<B>::pointee;
    }

    // Two function pointer types that are called the same way: as many parameters, each and the return
    // type passed alike.
    template <class F, class G>
    struct SameCall : std::false_type {};

    template <class R1, class... A1, class R2, class... A2>
    struct SameCall<R1 (*)(A1...), R2 (*)(A2...)>
        : std::bool_constant<
              sizeof...(A1) == sizeof...(A2) && SameAbi<R1, R2>() && (SameAbi<A1, A2>() && ...)> {};

}

// A struct's size and alignment.
#define CHECK_TYPE(Ours, Theirs)                                           \
    static_assert(sizeof(Ours) == sizeof(Theirs), #Ours ": size differs"); \
    static_assert(alignof(Ours) == alignof(Theirs), #Ours ": alignment differs")

// A field's offset and size.
#define CHECK_FIELD(Ours, ours, Theirs, theirs)                                                           \
    static_assert(offsetof(Ours, ours) == offsetof(Theirs, theirs), #Ours "::" #ours ": offset differs"); \
    static_assert(sizeof(Ours::ours) == sizeof(Theirs::theirs), #Ours "::" #ours ": size differs")

// A function table entry's place and how it is called.
#define CHECK_ENTRY(name)                                                                                 \
    static_assert(offsetof(PvrInterface, name) == offsetof(pvrInterface, name), #name ": place differs"); \
    static_assert(                                                                                        \
        SameCall<decltype(PvrInterface::name), decltype(pvrInterface::name)>::value,                      \
        #name ": called differently"                                                                      \
    )

// Constants.
static_assert(std::string_view(kPvrClientDll) == PVRCLIENT_DLL_NAME, "client library name");
static_assert(std::string_view(kPvrGetInterfaceExport) == PVR_GET_INTERFACE_FUNC_NAME, "export name");
static_assert(kPvrInterfaceMajor == PVR_MAJOR_VERSION, "interface major version");
static_assert(kPvrInterfaceMinor == PVR_MINOR_VERSION, "interface minor version");
static_assert(sizeof(PvrResult) == sizeof(pvrResult), "result code size");
static_assert(kPvrSuccess == pvr_success, "kPvrSuccess");
static_assert(kPvrFailed == pvr_failed, "kPvrFailed");
static_assert(kPvrDllFailed == pvr_dll_failed, "kPvrDllFailed");
static_assert(kPvrDllWrong == pvr_dll_wrong, "kPvrDllWrong");
static_assert(kPvrInterfaceNotFound == pvr_interface_not_found, "kPvrInterfaceNotFound");
static_assert(sizeof(PvrEye) == sizeof(pvrEyeType), "eye size");
static_assert(kPvrEyeLeft == pvrEye_Left, "kPvrEyeLeft");
static_assert(kPvrEyeRight == pvrEye_Right, "kPvrEyeRight");
static_assert(kPvrOrientationTracked == pvrStatus_OrientationTracked, "kPvrOrientationTracked");
static_assert(std::is_same<PvrHmd, pvrHmdHandle>::value, "headset handle");

// The data types.
CHECK_TYPE(PvrQuat, pvrQuatf);
CHECK_FIELD(PvrQuat, x, pvrQuatf, x);
CHECK_FIELD(PvrQuat, y, pvrQuatf, y);
CHECK_FIELD(PvrQuat, z, pvrQuatf, z);
CHECK_FIELD(PvrQuat, w, pvrQuatf, w);

CHECK_TYPE(PvrVector3, pvrVector3f);
CHECK_FIELD(PvrVector3, x, pvrVector3f, x);
CHECK_FIELD(PvrVector3, y, pvrVector3f, y);
CHECK_FIELD(PvrVector3, z, pvrVector3f, z);

CHECK_TYPE(PvrPose, pvrPosef);
CHECK_FIELD(PvrPose, orientation, pvrPosef, Orientation);
CHECK_FIELD(PvrPose, position, pvrPosef, Position);

CHECK_TYPE(PvrFov, pvrFovPort);
CHECK_FIELD(PvrFov, upTan, pvrFovPort, UpTan);
CHECK_FIELD(PvrFov, downTan, pvrFovPort, DownTan);
CHECK_FIELD(PvrFov, leftTan, pvrFovPort, LeftTan);
CHECK_FIELD(PvrFov, rightTan, pvrFovPort, RightTan);

CHECK_TYPE(PvrDisplayInfo, pvrDisplayInfo);
CHECK_FIELD(PvrDisplayInfo, refreshRate, pvrDisplayInfo, refresh_rate);
CHECK_FIELD(PvrDisplayInfo, adapterLuid, pvrDisplayInfo, luid);

CHECK_TYPE(PvrEyeRenderInfo, pvrEyeRenderInfo);
CHECK_FIELD(PvrEyeRenderInfo, fov, pvrEyeRenderInfo, Fov);
CHECK_FIELD(PvrEyeRenderInfo, hmdToEye, pvrEyeRenderInfo, HmdToEyePose);

CHECK_TYPE(PvrHmdStatus, pvrHmdStatus);
CHECK_FIELD(PvrHmdStatus, shouldQuit, pvrHmdStatus, ShouldQuit);

CHECK_TYPE(PvrPoseState, pvrPoseStatef);
CHECK_FIELD(PvrPoseState, pose, pvrPoseStatef, ThePose);
CHECK_FIELD(PvrPoseState, statusFlags, pvrPoseStatef, StatusFlags);

CHECK_TYPE(PvrTrackingState, pvrTrackingState);
CHECK_FIELD(PvrTrackingState, head, pvrTrackingState, HeadPose);

// The function table and the function that hands it out.
CHECK_TYPE(PvrInterface, pvrInterface);
CHECK_ENTRY(initialise);
CHECK_ENTRY(shutdown);
CHECK_ENTRY(createHmd);
CHECK_ENTRY(destroyHmd);
CHECK_ENTRY(getTimeSeconds);
CHECK_ENTRY(getEyeDisplayInfo);
CHECK_ENTRY(getEyeRenderInfo);
CHECK_ENTRY(getHmdStatus);
CHECK_ENTRY(getTrackingState);
CHECK_ENTRY(setIntConfig);
CHECK_ENTRY(getPredictedDisplayTime);
CHECK_ENTRY(getInt64Config);
static_assert(SameCall<PvrGetInterface, getPvrInterface_Fn>::value, "getPvrInterface: called differently");

int main() {
    printf(
        "The PVR declarations match Pimax's SDK headers (interface %u.%u, function table %zu bytes).\n",
        kPvrInterfaceMajor,
        kPvrInterfaceMinor,
        sizeof(PvrInterface)
    );

    return 0;
}
