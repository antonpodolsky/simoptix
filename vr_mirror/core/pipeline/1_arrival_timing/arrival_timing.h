// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Stage 1, arrival timing: each game frame's arrival with the timing noise taken off, and the
// display slot it was rendered for while the game keeps up. Runs on PimaxSource's poll thread as
// frames arrive; the result goes into the frame's record (GameFrame smoothTime, slotTime).
// Interface only; implementations beside it. Knows nothing about PVR, threads or later stages.
#pragma once

#include "pipeline/pipeline_types.h"

class ArrivalTiming {
public:
    virtual ~ArrivalTiming() = default;
    // Forget what was learnt (a new game, a new session).
    virtual void Reset() = 0;
    // A frame arrived (ArrivalInput: when, what the source knows of its slot, the display period).
    virtual ArrivalResult Add(const ArrivalInput& in) = 0;
};
