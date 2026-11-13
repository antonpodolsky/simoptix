// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "vr_mirror/frame_sources/demo/recorded_lap.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

#include "shared/local_files.h"

namespace {

    // Closes the file it was given.
    struct File {
        FILE* f = nullptr;
        ~File() {
            if (f) {
                fclose(f);
            }
        }
    };

    // Reads one "<tag>,..." line's numbers into `values` (up to `max`); returns how many.
    int Numbers(const char* line, double* values, int max) {
        int n = 0;
        const char* p = strchr(line, ',');

        while (p && n < max) {
            values[n++] = atof(p + 1);
            p = strchr(p + 1, ',');
        }

        return n;
    }

    Quat QuatOf(const double* xyzw) {
        return Normalize(
            Quat{
                static_cast<float>(xyzw[0]),
                static_cast<float>(xyzw[1]),
                static_cast<float>(xyzw[2]),
                static_cast<float>(xyzw[3]),
            }
        );
    }

}

bool NewestRecordedLap(std::wstring* stem) {
    wchar_t path[MAX_PATH];

    if (!NewestLocalFile(LocalFileKind::kRecordedLap, path, MAX_PATH)) {
        return false;
    }

    std::wstring s = path;
    const size_t dot = s.find_last_of(L'.');

    if (dot == std::wstring::npos) {
        return false;
    }

    s.resize(dot);
    *stem = s;
    return GetFileAttributesW((s + L".mp4").c_str()) != INVALID_FILE_ATTRIBUTES
           && GetFileAttributesW((s + L".csv").c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool WriteRecordedLap(const std::wstring& stem, const RecordedLap& lap, std::string* error) {
    File file;

    if (_wfopen_s(&file.f, (stem + L".csv").c_str(), L"w") != 0 || !file.f) {
        *error = "could not write the recorded lap's poses";
        return false;
    }

    const EyeInfo& e = lap.eye;
    fputs(
        "# SimOptix recorded lap: the frames of the .mp4 of the same name, one per video frame\n"
        "# eye,leftTan,rightTan,upTan,downTan,qx,qy,qz,qw,encodedTwice: the eye's field of view and its turn "
        "in the head\n"
        "# size,width,height; period,s: the headset's refresh; length,s: line to line\n"
        "# F,t,qx,qy,qz,qw: a frame's moment (s from the line) and the head pose it was drawn with\n",
        file.f
    );

    fprintf(
        file.f,
        "eye,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%d\n",
        e.leftTan,
        e.rightTan,
        e.upTan,
        e.downTan,
        e.hmdToEye.x,
        e.hmdToEye.y,
        e.hmdToEye.z,
        e.hmdToEye.w,
        e.encodedTwice ? 1 : 0
    );

    fprintf(file.f, "size,%d,%d\nperiod,%.9f\nlength,%.6f\n", lap.width, lap.height, lap.period, lap.length);

    for (const RecordedLapFrame& f : lap.frames) {
        fprintf(file.f, "F,%.6f,%.7f,%.7f,%.7f,%.7f\n", f.t, f.pose.x, f.pose.y, f.pose.z, f.pose.w);
    }

    if (ferror(file.f)) {
        *error = "could not write the recorded lap's poses";
        return false;
    }

    return true;
}

bool ReadRecordedLap(const std::wstring& stem, RecordedLap* lap, std::string* error) {
    File file;

    if (_wfopen_s(&file.f, (stem + L".csv").c_str(), L"r") != 0 || !file.f) {
        *error = "could not read the recorded lap's poses";
        return false;
    }

    *lap = RecordedLap();
    char line[512];
    double v[9];

    while (fgets(line, sizeof(line), file.f)) {
        if (strncmp(line, "F,", 2) == 0 && Numbers(line, v, 5) == 5) {
            lap->frames.push_back({v[0], QuatOf(v + 1)});
        } else if (strncmp(line, "eye,", 4) == 0 && Numbers(line, v, 9) == 9) {
            EyeInfo& e = lap->eye;
            e.leftTan = static_cast<float>(v[0]), e.rightTan = static_cast<float>(v[1]);
            e.upTan = static_cast<float>(v[2]), e.downTan = static_cast<float>(v[3]);
            e.hmdToEye = QuatOf(v + 4);
            e.encodedTwice = v[8] != 0;
        } else if (strncmp(line, "size,", 5) == 0 && Numbers(line, v, 2) == 2) {
            lap->width = static_cast<int>(v[0]), lap->height = static_cast<int>(v[1]);
        } else if (strncmp(line, "period,", 7) == 0 && Numbers(line, v, 1) == 1) {
            lap->period = v[0];
        } else if (strncmp(line, "length,", 7) == 0 && Numbers(line, v, 1) == 1) {
            lap->length = v[0];
        }
    }

    if (lap->frames.size() < 2 || lap->width <= 0 || lap->height <= 0 || lap->period <= 0
        || lap->length <= lap->frames.back().t) {
        *error = "the recorded lap's poses are incomplete";
        return false;
    }

    return true;
}
