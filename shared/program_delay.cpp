// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "shared/program_delay.h"

#include <cmath>

#include "shared/shared_memory.h"

void SetProgramDelay(double seconds) {
    InterlockedExchange64(&TheSharedMemory()->programDelayUs, LONG64(std::llround(seconds * 1e6)));
}

double ProgramDelay() {
    // An aligned 64-bit read is atomic on x64.
    return double(TheSharedMemory()->programDelayUs) * 1e-6;
}
