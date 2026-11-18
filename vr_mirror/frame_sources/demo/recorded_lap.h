// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// RecordedLap: a lap recorded for the demo, as VR Mirror keeps it under %LOCALAPPDATA%\simoptix\recorded_laps
// (shared\local_files.h): the left eye's images in a video file (<stem>.mp4, one video frame per game
// frame) and this, in <stem>.csv: the eye's shape, the headset's refresh period, and each frame's moment
// and the head pose it was drawn with. The OBS plugin's lap recorder writes it
// (vr_mirror\obs_plugin\lap_recorder\lap_recorder.h), and the demo source plays it (demo_source.h).
//
// Plain data and its file: knows nothing about video, D3D or OBS.
#pragma once

#include <string>
#include <vector>

#include "common/game_frame.h"
#include "common/rot.h"

struct RecordedLapFrame {
    double t = 0;  // seconds from the lap's start: the moment it was drawn for
    Quat pose;     // the head pose it was drawn with
};

struct RecordedLap {
    EyeInfo eye;
    int width = 0, height = 0;  // the images' size (pixels)
    double period = 1.0 / 90;   // the headset's refresh period (s)
    double length = 0;          // s, from the line to the line
    std::vector<RecordedLapFrame> frames;
};

// The newest lap's two paths, without their endings: false if none was recorded.
bool NewestRecordedLap(std::wstring* stem);
// <stem>.csv.
bool WriteRecordedLap(const std::wstring& stem, const RecordedLap& lap, std::string* error);
bool ReadRecordedLap(const std::wstring& stem, RecordedLap* lap, std::string* error);
