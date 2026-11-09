// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// PipelineConfig: which implementation each stage of the frame pipeline uses, one field per stage,
// named as the stage's folder (vr_mirror\core\pipeline\<N>_<stage>\), its values as the implementations.
// ConfigFor maps the settings onto it; NameOf and Parse give each value's name, for logs and
// stage_replay's flags. Plain data and lookups only: it knows the implementations' names, not their
// code (pipeline_builder makes them).
#pragma once

#include <cstring>
#include <initializer_list>

#include "common/settings.h"

enum class ArrivalTimingKind { kGrid, kReported };
enum class FramePickerKind { kFixedDelay, kNewestFrame, kHeldFrame };
// How a host draws: on its own clock's ticks (the OBS plugin), or once per game frame (the mirror app);
// it decides the frame picker.
enum class OutputClock { kTicks, kFrames };
// What the frame source tells of each frame: only when it arrived (the Pimax client mirror), or also
// the display slot it was drawn for and the pose it was drawn with (the OpenXR layer). It decides
// stages 1 and 2: with a reported slot and pose there is nothing to guess or judge.
enum class FrameReport { kArrival, kSlotAndPose };
// Settings drawn_pose 1, 0, 2.
enum class DrawnPoseKind { kSlotCheck, kArrivalSlot, kCockpit, kReported };
enum class SmoothingKind { kInstant = 0, kLookahead = 1 };  // settings smoothing 0 and 1, 2 and above
enum class WarpKind { kRotation };
// Debugging beside the pipeline (vr_mirror\core\pipeline\observer): none, or the frame-number stamp and the
// frame trace (a host's debug setting).
enum class ObserverKind { kNone, kStampTrace };

struct PipelineConfig {
    ArrivalTimingKind arrivalTiming = ArrivalTimingKind::kGrid;
    FramePickerKind framePicker = FramePickerKind::kFixedDelay;
    DrawnPoseKind drawnPose = DrawnPoseKind::kSlotCheck;
    SmoothingKind smoothing = SmoothingKind::kLookahead;
    WarpKind warp = WarpKind::kRotation;
    ObserverKind observer = ObserverKind::kNone;
};

inline bool operator==(const PipelineConfig& a, const PipelineConfig& b) {
    return a.arrivalTiming == b.arrivalTiming && a.framePicker == b.framePicker && a.drawnPose == b.drawnPose
           && a.smoothing == b.smoothing && a.warp == b.warp && a.observer == b.observer;
}

inline bool operator!=(const PipelineConfig& a, const PipelineConfig& b) {
    return !(a == b);
}

// The configuration the settings ask for, for a host drawing on `clock` from a frame source that tells
// `report` of each frame. Smoothing off runs as Instant (no delay); the pipeline then shows the
// headset's view as it is (Stabilizes). A reported slot and pose take stage 1's and stage 2's reported
// methods, whatever the drawn pose setting says.
inline PipelineConfig ConfigFor(
    const MirrorSettings& s,
    OutputClock clock = OutputClock::kTicks,
    FrameReport report = FrameReport::kArrival
) {
    PipelineConfig c;
    c.drawnPose = s.drawnPose <= 0   ? DrawnPoseKind::kArrivalSlot
                  : s.drawnPose == 1 ? DrawnPoseKind::kSlotCheck
                                     : DrawnPoseKind::kCockpit;

    if (report == FrameReport::kSlotAndPose) {
        c.arrivalTiming = ArrivalTimingKind::kReported;
        c.drawnPose = DrawnPoseKind::kReported;
    }

    c.smoothing = s.smoothing <= MirrorSettings::kSmoothingInstant ? SmoothingKind::kInstant
                                                                   : SmoothingKind::kLookahead;

    c.framePicker = clock == OutputClock::kTicks             ? FramePickerKind::kFixedDelay
                    : c.smoothing == SmoothingKind::kInstant ? FramePickerKind::kNewestFrame
                                                             : FramePickerKind::kHeldFrame;

    return c;
}

inline const char* NameOf(ArrivalTimingKind k) {
    return k == ArrivalTimingKind::kGrid ? "grid" : k == ArrivalTimingKind::kReported ? "reported" : "?";
}

inline bool Parse(const char* name, ArrivalTimingKind* k) {
    for (ArrivalTimingKind v : {ArrivalTimingKind::kGrid, ArrivalTimingKind::kReported}) {
        if (std::strcmp(name, NameOf(v)) == 0) {
            return *k = v, true;
        }
    }

    return false;
}

inline const char* NameOf(FramePickerKind k) {
    return k == FramePickerKind::kFixedDelay    ? "fixed_delay"
           : k == FramePickerKind::kNewestFrame ? "newest_frame"
           : k == FramePickerKind::kHeldFrame   ? "held_frame"
                                                : "?";
}

inline bool Parse(const char* name, FramePickerKind* k) {
    for (FramePickerKind v :
         {FramePickerKind::kFixedDelay, FramePickerKind::kNewestFrame, FramePickerKind::kHeldFrame}) {
        if (std::strcmp(name, NameOf(v)) == 0) {
            return *k = v, true;
        }
    }

    return false;
}

inline const char* NameOf(DrawnPoseKind k) {
    return k == DrawnPoseKind::kSlotCheck     ? "slot_check"
           : k == DrawnPoseKind::kArrivalSlot ? "arrival_slot"
           : k == DrawnPoseKind::kCockpit     ? "cockpit"
           : k == DrawnPoseKind::kReported    ? "reported"
                                              : "?";
}

inline bool Parse(const char* name, DrawnPoseKind* k) {
    for (DrawnPoseKind v :
         {DrawnPoseKind::kSlotCheck,
          DrawnPoseKind::kArrivalSlot,
          DrawnPoseKind::kCockpit,
          DrawnPoseKind::kReported}) {
        if (std::strcmp(name, NameOf(v)) == 0) {
            return *k = v, true;
        }
    }

    return false;
}

inline const char* NameOf(SmoothingKind k) {
    return k == SmoothingKind::kInstant ? "instant" : k == SmoothingKind::kLookahead ? "lookahead" : "?";
}

inline bool Parse(const char* name, SmoothingKind* k) {
    for (SmoothingKind v : {SmoothingKind::kInstant, SmoothingKind::kLookahead}) {
        if (std::strcmp(name, NameOf(v)) == 0) {
            return *k = v, true;
        }
    }

    return false;
}

inline const char* NameOf(WarpKind k) {
    return k == WarpKind::kRotation ? "rotation" : "?";
}
inline bool Parse(const char* name, WarpKind* k) {
    if (std::strcmp(name, NameOf(WarpKind::kRotation)) != 0) {
        return false;
    }
    *k = WarpKind::kRotation;
    return true;
}

inline const char* NameOf(ObserverKind k) {
    return k == ObserverKind::kNone ? "none" : k == ObserverKind::kStampTrace ? "stamp_trace" : "?";
}

inline bool Parse(const char* name, ObserverKind* k) {
    for (ObserverKind v : {ObserverKind::kNone, ObserverKind::kStampTrace}) {
        if (std::strcmp(name, NameOf(v)) == 0) {
            return *k = v, true;
        }
    }

    return false;
}
