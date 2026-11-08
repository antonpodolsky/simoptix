// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The Pimax PVR client interface, as far as VR Mirror uses it, declared by this project. Pimax Play
// installs the client library (libPVRClient64.dll); its one export, getPvrInterface, hands out a
// table of functions for the interface version asked for, and VR Mirror asks for 1.26. Only what the
// Pimax frame source and the e2e fake runtime use has a name here. The table's other entries, and the
// fields nobody reads, are unnamed placeholders that keep the named ones in their places.
//
// The layout must match the client library's byte for byte: the order of the table's entries, the
// size, alignment and field offsets of every type, and the values of the constants. Every function
// uses the one x64 calling convention, so this is declared for 64-bit builds only.
// pvr_layout_check\ compares all of it with Pimax's own SDK headers, where they are installed.
//
// Knows nothing about sessions, mirrors or poses (pvr_session.*, pvr_mirror.*).
#pragma once

#include <windows.h>

#include <cstdint>

static_assert(sizeof(void*) == 8, "the PVR client interface is declared for 64-bit builds only");

// The client library, its export, and the interface version VR Mirror asks it for (major, minor).
constexpr const char* kPvrClientDll = "libPVRClient64.dll";
constexpr const char* kPvrGetInterfaceExport = "getPvrInterface";
constexpr uint32_t kPvrInterfaceMajor = 1;
constexpr uint32_t kPvrInterfaceMinor = 26;

// Result codes. The runtime's functions return the first two; the other three are what loading the
// client library returns when it fails (pvr_session.cpp), with the numbers Pimax uses for them.
using PvrResult = int32_t;
constexpr PvrResult kPvrSuccess = 0;
constexpr PvrResult kPvrFailed = 1;
constexpr PvrResult kPvrDllFailed = 2;          // the client library could not be loaded
constexpr PvrResult kPvrDllWrong = 3;           // it has no getPvrInterface
constexpr PvrResult kPvrInterfaceNotFound = 4;  // it doesn't offer the version asked for

// Which eye.
using PvrEye = int32_t;
constexpr PvrEye kPvrEyeLeft = 0;
constexpr PvrEye kPvrEyeRight = 1;

// A bit of PvrPoseState::statusFlags: the orientation is tracked.
constexpr uint32_t kPvrOrientationTracked = 0x1;

// A headset opened by createHmd: the runtime's own pointer, never looked into.
using PvrHmd = void*;

struct PvrQuat {
    float x, y, z, w;
};

struct PvrVector3 {
    float x, y, z;
};

struct PvrPose {
    PvrQuat orientation;
    PvrVector3 position;  // metres
};

// An eye's field of view, as the tangents of its half-angles from the view's centre.
struct PvrFov {
    float upTan, downTan, leftTan, rightTan;
};

// What getEyeDisplayInfo fills in.
struct alignas(8) PvrDisplayInfo {
    int32_t unused[9];
    float refreshRate;  // Hz
    LUID adapterLuid;   // the graphics adapter the headset is connected to
};

// What getEyeRenderInfo fills in.
struct alignas(8) PvrEyeRenderInfo {
    PvrFov fov;
    float unused0[2];
    PvrPose hmdToEye;  // the eye relative to the head; its orientation is the eye's cant
    int32_t unused1[4];
};

// What getHmdStatus fills in.
struct alignas(8) PvrHmdStatus {
    char unused[5];
    char shouldQuit;  // non-zero: the runtime asks this session to close (another VR app takes over)
};

// A tracked device's pose at one moment.
struct alignas(8) PvrPoseState {
    PvrPose pose;
    float unused0[12];
    double unused1;
    uint32_t statusFlags;  // kPvrOrientationTracked, ...
};

// What getTrackingState fills in: the head first.
struct alignas(8) PvrTrackingState {
    PvrPoseState head;
    PvrPoseState unused[2];
};

// The function table the client library hands out, in interface version 1.26. Times are the
// runtime's clock, in seconds.
struct PvrInterface {
    PvrResult (*initialise)();
    void (*shutdown)();
    PvrResult (*createHmd)(PvrHmd* hmd);
    void (*destroyHmd)(PvrHmd hmd);
    void* unused4;
    double (*getTimeSeconds)();
    void* unused6;
    PvrResult (*getEyeDisplayInfo)(PvrHmd hmd, PvrEye eye, PvrDisplayInfo* info);
    PvrResult (*getEyeRenderInfo)(PvrHmd hmd, PvrEye eye, PvrEyeRenderInfo* info);
    PvrResult (*getHmdStatus)(PvrHmd hmd, PvrHmdStatus* status);
    void* unused10[3];
    // The poses for time `time`: measured, or predicted for a time still to come.
    PvrResult (*getTrackingState)(PvrHmd hmd, double time, PvrTrackingState* state);
    void* unused14[5];
    PvrResult (*setIntConfig)(PvrHmd hmd, const char* key, int value);
    void* unused20[2];
    // The display slot a frame would be shown at; frame 0: the one a game would render now.
    double (*getPredictedDisplayTime)(PvrHmd hmd, int64_t frame);
    void* unused23[37];
    int64_t (*getInt64Config)(PvrHmd hmd, const char* key, int64_t defaultValue);
    void* unused61[18];
};

// getPvrInterface: the table for interface version major.minor, or null if the library doesn't
// offer it.
using PvrGetInterface = PvrInterface* (*)(uint32_t major, uint32_t minor);
