// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// ProgramDelay: how far the view runs behind the game (seconds), so everything follows the same
// moment: the telemetry is handed out as of the moment the view shows (shared\telemetry\telemetry.h,
// for the G-meter and race auto-record), and the audio is shifted by it. VR Mirror sets it once its
// output delay is known (0 while it isn't running; with several VR Mirror sources shown, the last one to set
// it). The number lives in the process's SharedMemory, so neither plugin loads the other, and with VR Mirror
// not installed it reads 0.
//
// Any thread.
#pragma once

void SetProgramDelay(double seconds);
double ProgramDelay();
