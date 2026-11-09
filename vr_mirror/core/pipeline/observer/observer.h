// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Observer: debugging beside the pipeline, not a stage. The pipeline glue hands it one record per
// output frame, collected from the stages' answers (the frame shown and its timing, the image's
// pose, the followed pose, the view), and the stages' shared stats (StatsReport) whenever a host
// asks for them; an observer turns them into whatever helps debugging (a trace row, the number the
// frame-number stamp shows). It never changes what the pipeline does. Interface and its data;
// implementations beside it, made by the builder (PipelineConfig::observer). Knows nothing about
// files, D3D, OBS or the stages' methods: a host gives it a place to write (TraceSink) and draws the
// stamp it numbers.
#pragma once

#include <cstdint>

#include "pipeline/pipeline_types.h"
#include "pipeline/stage_stats.h"

// Where an observer writes its trace: a host's file, a line at a time (no newline).
class TraceSink {
public:
    virtual void Line(const char* line) = 0;

protected:
    ~TraceSink() = default;
};

// One output frame, as the pipeline produced it (times in source seconds). Held by value until its row
// is written (StampTraceObserver), so it owns its notes.
struct FrameRecord {
    double tick = 0;                        // the host's tick, or when it drew
    double draw = 0;                        // when the pipeline ran (the host's `now`)
    uint64_t serial = 0;                    // the game frame shown
    double arrival = 0, smoothArrival = 0;  // its arrival, and de-jittered (GameFrame)
    double slot = 0;                        // the display slot it arrived for
    double slotLead = 0;                    // the runtime's next slot minus its arrival
    double drawnSlot = 0;                   // the slot the frame source says it was drawn for; 0 unknown
    Quat img;                    // the pose the shown image was drawn with (the drawn pose's answer)
    Quat raw;                    // the pose the view followed (the frame's, or a tick's own moment's)
    Quat view;                   // the view shown
    const char* drawnPose = "";  // how the image's pose was found (PipelineConfig's name)
    float zoom = 0;
    // so far: the smoothing's last resort stopped the view; it steered
    unsigned edgeClamps = 0, edgePlanned = 0;
    double viewTime = 0;          // the moment the view is for when not the shown frame's own (ticks); else 0
    uint64_t followedSerial = 0;  // the game frame the view followed (serial when no older image stands in)
    // how far the smoothing's last resort moved this frame's view (degrees; 0: it didn't)
    float edgeShiftDeg = 0;
    float aspect = 0;  // the output's width / height
    // What the drawn pose stage knows of the shown image's pose (DrawnPose::Describe:
    // "name=value" pairs separated by spaces, method-specific), when the observer wants it (WantsNotes).
    char poseNotes[160] = {};
};

// A StatsReport into a fixed buffer, for one frame's notes: "name=value" pairs separated by spaces
// (topics are left out; names keep their own words with spaces turned into underscores). Nothing
// allocated; what doesn't fit is cut off.
class NotesReport : public StatsReport {
public:
    NotesReport(char* text, int size) : text_(text), size_(size) {
        if (size_ > 0) {
            text_[0] = 0;
        }
    }

    void Line(const char*) override {}
    void Value(const char* name, double value, int decimals = 0) override {
        if (used_ > 0) {
            Put(' ');
        }
        for (const char* c = name; *c; c++) {
            Put(*c == ' ' || *c == ',' ? '_' : *c);
        }
        char number[32];
        std::snprintf(number, sizeof(number), "=%.*f", decimals, value);
        for (const char* c = number; *c; c++) {
            Put(*c);
        }
    }

private:
    void Put(char c) {
        if (used_ >= size_ - 1) {
            return;
        }
        text_[used_++] = c;
        text_[used_] = 0;
    }

    char* text_;
    int size_;
    int used_ = 0;
};

class Observer {
public:
    virtual ~Observer() = default;
    // The host's trace to write to, or null (none, or closed: what is pending is written first).
    virtual void SetSink(TraceSink*) {}
    // One output frame.
    virtual void Frame(const FrameRecord&) {}
    // The host presented the last frame at `t` (the mirror app's window); a host that doesn't present
    // (OBS draws itself) never calls it.
    virtual void Presented(double /*t*/) {}
    // Whether it takes the stages' stats (StatsLine), and one line of them (LineStatsReport's).
    virtual bool WantsStats() const { return false; }
    // Whether the glue should fill FrameRecord::poseNotes (it asks the drawn pose stage per frame).
    virtual bool WantsNotes() const { return false; }
    virtual void StatsLine(const char*) {}
    // The number the frame-number stamp shows for the last frame (15 bits); 0 for an observer that
    // numbers none.
    virtual uint32_t Stamp() const { return 0; }
};
