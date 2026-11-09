// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "pipeline/observer/stamp_trace_observer.h"

#include <cstdio>

// Times in source seconds; quaternions x, y, z, w. stamp: the number the frame-number stamp shows; tick:
// the host's tick (OBS) or draw; slot: the display slot the frame arrived for; img: the pose its
// image was drawn with; raw: the pose the view followed; drawn_pose: how img was found; draw,
// presented: when the pipeline ran and the frame was presented (0: the host doesn't present);
// edge_clamps: so far, frames where the smoothing's last resort stopped the view at an edge of the
// shown image or the followed one; edge_planned: so far, frames whose smoothing steered its path;
// view_time: the moment the view is for on a tick's own timeline (0: the frame's own);
// followed_serial: the game frame the view followed (serial unless an older image is shown again);
// edge_shift: how far the last resort moved this frame's view (degrees); aspect: the output's width /
// height; drawn_slot: the display slot the frame source says the frame was drawn for (0: it doesn't say);
// pose_notes: the drawn pose stage's own name=value pairs (space-separated; no commas).
const char* const StampTraceObserver::kColumns =
    "stamp,tick,serial,arrival,smooth,slot,img_x,img_y,img_z,img_w,raw_x,raw_y,raw_z,raw_w,view_x,view_y,"
    "view_z,view_w,drawn_pose,draw,presented,zoom,edge_clamps,slot_lead,edge_planned,view_time,"
    "followed_serial,edge_shift,aspect,drawn_slot,pose_notes";

void StampTraceObserver::SetSink(TraceSink* sink) {
    if (sink == sink_) {
        return;  // the same trace goes on (a rebuilt pipeline)
    }
    Flush();
    sink_ = sink;
    if (sink_) {
        sink_->Line(kColumns);
    }
}

void StampTraceObserver::Frame(const FrameRecord& r) {
    Flush();
    pending_ = r;
    pendingStamp_ = ++frames_ & 0x7fff;
    presented_ = 0;
    hasPending_ = true;
}

void StampTraceObserver::Presented(double t) {
    if (!hasPending_) {
        return;
    }
    presented_ = t;
    Flush();
}

void StampTraceObserver::StatsLine(const char* line) {
    if (!sink_) {
        return;
    }
    char row[600];
    std::snprintf(row, sizeof(row), "#stats,%s", line);
    sink_->Line(row);
}

void StampTraceObserver::Flush() {
    if (!hasPending_) {
        return;
    }
    hasPending_ = false;
    if (!sink_) {
        return;
    }

    const FrameRecord& r = pending_;
    char row[800];
    std::snprintf(
        row,
        sizeof(row),
        "%u,%.6f,%llu,%.6f,%.6f,%.6f,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,"
        "%.7f,%s,%.6f,%.6f,%.4f,%u,%.6f,%u,%.6f,%llu,%.4f,%.4f,%.6f,%s",
        pendingStamp_,
        r.tick,
        static_cast<unsigned long long>(r.serial),
        r.arrival,
        r.smoothArrival,
        r.slot,
        r.img.x,
        r.img.y,
        r.img.z,
        r.img.w,
        r.raw.x,
        r.raw.y,
        r.raw.z,
        r.raw.w,
        r.view.x,
        r.view.y,
        r.view.z,
        r.view.w,
        r.drawnPose,
        r.draw,
        presented_,
        r.zoom,
        r.edgeClamps,
        r.slotLead,
        r.edgePlanned,
        r.viewTime,
        static_cast<unsigned long long>(r.followedSerial),
        r.edgeShiftDeg,
        r.aspect,
        r.drawnSlot,
        r.poseNotes
    );

    sink_->Line(row);
}
