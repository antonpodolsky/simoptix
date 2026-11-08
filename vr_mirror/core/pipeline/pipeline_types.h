// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The plain data the frame pipeline's stages pass to each other, and the small shared helpers
// every stage needs the same way: a frame's times settled once (FrameTimes: which of a frame's
// times each stage uses), the output's geometry from the eye and the settings, the
// rotation that takes an output ray into the eye camera a picture was drawn from, and the level
// horizon. Plain data and inline maths only: no state, no I/O, and nothing about any one stage's
// implementation (vr_mirror\core\pipeline\README.md has the stages and their order).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "common/game_frame.h"
#include "common/rot.h"
#include "common/settings.h"

// What stage 1 knows of an arriving frame (the frame source's clock, seconds).
struct ArrivalInput {
    double arrival = 0;       // when it arrived
    double nextSlot = 0;      // the runtime's next display slot at that moment; 0 unknown
    double reportedSlot = 0;  // the display slot the frame source says it was drawn for; 0 unknown
    double period = 0;        // the display's
};

// Stage 1's answer for an arriving frame (the frame source's clock, seconds).
struct ArrivalResult {
    double smooth = 0;  // the arrival with the timing noise removed (locked to the display grid)
    double slot = 0;    // the display slot it was rendered for (while the game keeps up)
    double offset = 0;  // the learnt offset from arrival to slot
};

// A host's stored frame as the frame picker sees it (source seconds).
struct StoredFrame {
    bool valid = false;  // the entry holds a finished copy
    uint64_t serial = 0;
    double arrival = 0, smoothTime = 0, slotTime = 0;  // as in GameFrame
    double ready = 0;                                  // when the copy was seen complete
    bool synced = false;  // its timing is known (frame signal seen; GameFrame::synced)
};

enum class PickKind {
    kNext,    // the next frame in order (or the first one due)
    kRepeat,  // the same frame again: the next one is not due yet
    kJump,    // skipped ahead: two or more were overdue
    kHeld,    // nothing due is ready: the last picture again
    kNone,    // nothing to show
};

// The frame picker's answer for one output tick.
struct Pick {
    int index = -1;  // the store entry to show (-1 with kNone)
    PickKind kind = PickKind::kNone;
    double moment = 0;  // the moment on the timeline the tick stands for: the tick minus the output delay
    double due = 0;     // when the shown frame was due
};

// A frame's times, settled once for the stages after it (source seconds). Which of a GameFrame's
// times each stage uses: TimesOf decides it.
struct FrameTimes {
    uint64_t serial = 0;
    bool synced = false;   // the frame's timing is known (frame signal seen); else it steps by the clock
    double step = 0;       // what the smoothing steps by: the de-jittered arrival once synced, else
                           // the arrival itself
    double poseTime = 0;   // the measured head's moment for it: its step
    double shownSlot = 0;  // the display slot it arrived for and is shown in; 0 unknown
    double drawnSlot = 0;  // the display slot the frame source says it was drawn for; 0 unknown (GameFrame)
    double arrival = 0, smoothArrival = 0, slotLead = 0;  // as in GameFrame (for the observer's record)
    double moment = 0;  // the moment on the timeline it stands for, a steady rhythm: the slot it is
                        // shown in, else its pose time; an OBS tick's own moment.
                        // Where the look-ahead's window is centred unless the drawn pose places
                        // its poses elsewhere (DrawnPose::MomentOf)
};

// A game frame's times.
inline FrameTimes TimesOf(const GameFrame& f) {
    FrameTimes t;
    t.serial = f.serial;
    t.synced = f.synced;
    t.step = f.synced ? f.smoothTime : f.time;
    t.poseTime = t.step;
    t.shownSlot = f.slotTime;
    t.drawnSlot = f.drawnSlot;
    t.moment = f.synced && f.slotTime > 0 ? f.slotTime : t.poseTime;
    t.arrival = f.time, t.smoothArrival = f.smoothTime, t.slotLead = f.slotLead;
    return t;
}

// The times of an output moment that is no game frame (the OBS plugin's tick: its own moment on the
// timeline, `serial` its own): synced, no slots.
inline FrameTimes MomentTimes(uint64_t serial, double moment) {
    FrameTimes t;
    t.serial = serial;
    t.synced = true;
    t.step = moment;
    t.poseTime = moment;
    t.moment = t.poseTime;
    return t;
}

// The middle of `eye`'s image, as the turn from the eye's camera to a camera that looks at it: halfway
// between the image's left and right edges, then halfway between its top and bottom, with no roll of its
// own. The view looks there, with as much image on each side. Frame sources describe the same image
// from different axes (Pimax: from the eye turned 2° outward, 50.1° out and 49.6° in; LMU through OpenXR:
// from straight ahead, 52.6° out and 47.6° in), and its middle is the same for all of them.
inline Quat ImageMiddle(const EyeInfo& eye) {
    const float yaw = (std::atan(eye.rightTan) - std::atan(eye.leftTan)) / 2;  // radians, towards +x
    const float pitch = (std::atan(eye.upTan) - std::atan(eye.downTan)) / 2;   // radians, towards +y
    const Quat turn = {0, -std::sin(yaw / 2), 0, std::cos(yaw / 2)};           // -yaw about +y: -z to +x
    const Quat tilt = {std::sin(pitch / 2), 0, 0, std::cos(pitch / 2)};        // about +x: -z to +y
    return Mul(turn, tilt);
}

// The output's view and the eye image it is cut from, for one frame: the eye's half-FOV tangents
// (left, right, up, down, positive), the eye's orientation in the head (its cant), the view's camera in
// the eye's (aim: the image's middle), the output's half-FOV tangents around it (outW, outH: the image's
// half-angle on each side divided by the zoom, at the output's aspect ratio, never taller than the
// image), whether the view's camera is held level (stabilization on), and whether the picture is
// sRGB-encoded twice (EyeInfo).
struct ViewGeometry {
    float left = 1, right = 1, up = 1, down = 1;
    float outW = 1, outH = 1;
    Quat hmdToEye;
    Quat aim;
    bool level = false;
    bool encodedTwice = false;
};

// `aspect`: the output's width / height.
inline ViewGeometry GeometryFor(const EyeInfo& eye, const MirrorSettings& s, float aspect) {
    ViewGeometry g;
    g.left = eye.leftTan, g.right = eye.rightTan;
    g.up = eye.upTan, g.down = eye.downTan;
    g.hmdToEye = eye.hmdToEye;
    g.aim = ImageMiddle(eye);
    g.level = Stabilizes(s);  // off: the headset's view as it is, roll included
    g.encodedTwice = eye.encodedTwice;

    // Seen from its middle, the image reaches the mean of its two half-angles on each side.
    const float wide = std::tan((std::atan(g.left) + std::atan(g.right)) / 2);
    const float tall = std::tan((std::atan(g.up) + std::atan(g.down)) / 2);
    g.outW = wide / s.zoom;
    g.outH = g.outW / aspect;

    if (g.outH > tall / s.zoom) {
        g.outH = tall / s.zoom;
        g.outW = g.outH * aspect;
    }

    return g;
}

// How wide (pixels) a picture of `eye` must be for an output `outputWidth` pixels wide (at `aspect`) to
// show it 1:1 in its middle: the output spans 2 outW of view tangent, the picture left + right. (The
// view's middle is a few degrees off the eye's axis, where the picture is denser by under 0.3%.)
inline float NativePictureWidth(const EyeInfo& eye, const MirrorSettings& s, int outputWidth, float aspect) {
    const ViewGeometry g = GeometryFor(eye, s, aspect);
    return float(outputWidth) * (g.left + g.right) / (2 * g.outW);
}

// `v` with its roll removed: looking the same way, world +Y up (the horizon kept level). Not exactly
// idempotent in float, so a view is levelled once.
inline Quat LevelHorizon(const Quat& v) {
    return LookRotation(Rotate(v, {0, 0, -1}));
}

// Output ray (the view's camera) -> the eye camera of a picture drawn at head pose `picture`, as a
// row-major 3x3 matrix. The view's camera is the head at `view`, turned by the eye's cant and then to
// the image's middle. With the view held level, that camera is levelled itself: a camera turned sideways
// from the head tilts whenever the head pitches, by about the turn times the sine of the pitch (2.0° at
// 3.5° and 35°).
inline void ViewToEye(const Quat& picture, const Quat& view, const ViewGeometry& g, float rm[3][3]) {
    const Quat turned = Mul(Mul(view, g.hmdToEye), g.aim);
    const Quat camera = g.level ? LevelHorizon(turned) : turned;
    ToMatrix(Mul(Conj(Mul(picture, g.hmdToEye)), camera), rm);
}

// The pipeline's previous output, which a smoothing with state follows on from: its drawn pose
// and its view (level), whether it came from this smoothing, the view lock or no
// stabilization.
struct PreviousView {
    bool has = false;
    Quat picture, view;
};
