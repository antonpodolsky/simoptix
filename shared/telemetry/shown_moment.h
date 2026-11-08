// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// ShownMoment: the moment of the game the picture shows, so the telemetry handed to the overlays and race
// auto-record matches the picture without them knowing about any delay. Logic only: the times come in.
//
// It never goes back: when the picture's delay grows (VR Mirror's view starting, or a smoother
// setting), the moment holds until the clock catches up, rather than replaying what was already shown;
// when the delay shrinks (the view hidden), it jumps forward.
#pragma once

#include <algorithm>

// `now`: performance-counter seconds. `delay`: how far the picture runs behind the game (s,
// shared\program_delay.h). `last`: the moment handed out last (0: none yet).
inline double ShownMoment(double now, double delay, double last) {
    return std::max(last, now - delay);
}
