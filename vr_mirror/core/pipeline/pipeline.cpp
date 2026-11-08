// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "pipeline/pipeline.h"

#include <algorithm>
#include <cmath>

void Pipeline::Start(Stages stages, const PipelineConfig& config) {
    TraceSink* sink = sink_;
    *this = Pipeline();
    stages_ = std::move(stages);
    config_ = config;
    SetTraceSink(sink);
}

void Pipeline::SetTraceSink(TraceSink* sink) {
    sink_ = sink;
    if (stages_.observer) {
        stages_.observer->SetSink(sink);
    }
}

void Pipeline::Swap(std::unique_ptr<Observer> s, ObserverKind k) {
    if (stages_.observer) {
        stages_.observer->SetSink(nullptr);  // what it has pending is written
    }
    stages_.observer = std::move(s);
    config_.observer = k;
    if (stages_.observer) {
        stages_.observer->SetSink(sink_);
    }
}

void Pipeline::Swap(std::unique_ptr<FramePicker> s, FramePickerKind k) {
    stages_.framePicker = std::move(s);
    config_.framePicker = k;
}

void Pipeline::Swap(std::shared_ptr<DrawnPose> s, DrawnPoseKind k) {
    stages_.drawnPose = std::move(s);
    config_.drawnPose = k;
}

void Pipeline::Swap(std::unique_ptr<Smoothing> s, SmoothingKind k) {
    stages_.smoothing = std::move(s);
    config_.smoothing = k;
}

void Pipeline::Swap(std::unique_ptr<Warp> s, WarpKind k) {
    stages_.warp = std::move(s);
    config_.warp = k;
}

void Pipeline::Report(StatsReport& r) {
    // The host's report, and the observer's lines of the same numbers.
    Observer* observer = stages_.observer.get();
    auto toObserver = [observer](const char* line) { observer->StatsLine(line); };
    LineStatsReport<decltype(toObserver)> traced(toObserver);

    struct Both : StatsReport {
        StatsReport* a;
        StatsReport* b;
        void Line(const char* topic) override { a->Line(topic), b ? b->Line(topic) : void(); }
        void Value(const char* name, double v, int decimals) override {
            a->Value(name, v, decimals);
            if (b) {
                b->Value(name, v, decimals);
            }
        }
    } both;

    both.a = &r;
    both.b = observer && observer->WantsStats() ? &traced : nullptr;
    stages_.framePicker->Report(both);
    stages_.drawnPose->Report(both);
    stages_.smoothing->Report(both);

    // The smoothing's last resort (Smoothed::stopped, counted here for every smoothing alike): the
    // frames it had to stop the view at an edge since the last report.
    if (stops_ != reportedStops_) {
        both.Line("edge limit");
        both.Value("frames", stops_ - reportedStops_, 0);
        reportedStops_ = stops_;
    }

    stages_.warp->Report(both);
}

void Pipeline::Reset() {
    stages_.smoothing->Reset();
    hasView_ = false;
}

WarpParams
Pipeline::Update(const MirrorSettings& s, PoseSource& poses, const EyeInfo& eye, const OutputRequest& r) {
    DrawnPose& drawnPose = *stages_.drawnPose;
    Smoothing& smoothing = *stages_.smoothing;
    const FrameTimes& f = r.view;

    const float dt = Step(f, r);

    const ViewGeometry g = GeometryFor(eye, s, r.aspect);
    const Quat picture = drawnPose.PoseOf(f, poses);

    // The pose the shown picture was drawn with: `picture` unless an older one stands in.
    const bool standIn = r.image && r.image->serial != f.serial;
    const Quat shown = standIn ? drawnPose.PoseOf(*r.image, poses) : picture;
    Quat view = picture;

    // how far the smoothing's last resort moved the view this frame (for the observer)
    float edgeShiftDeg = 0;
    if (Stabilizes(s)) {
        SmoothingInput in;
        in.picture = picture;
        in.dt = dt;
        in.previous = {hasView_, lastRaw_, lastView_};
        in.shown = shown;
        in.geometry = g;

        FillPath(&in, poses, f, r, standIn, dt);

        // The view, inside the pictures: nothing after the smoothing changes it.
        const Smoothed out = smoothing.Smooth(in);
        planned_ += out.steered;
        stops_ += out.stopped;
        edgeShiftDeg = out.stopDeg;
        view = out.view;
    }

    hasView_ = true;
    lastRaw_ = picture;
    lastView_ = view;
    lastImage_ = shown;
    zoom_ = s.zoom;

    // The observer's record of this frame (debugging; the default observer drops it).
    if (Observer* observer = stages_.observer.get()) {
        Record(*observer, s, f, r, {shown, picture, view}, edgeShiftDeg);
    }

    return stages_.warp->Params(shown, view, g);
}

float Pipeline::Step(const FrameTimes& f, const OutputRequest& r) {
    // Step the smoothing by the frames' own spacing, not by when the host happened to draw.
    float dt = float(f.synced && lastStep_ > 0 ? f.step - lastStep_ : r.now - lastUpdate_);
    dt = std::clamp(dt, 0.001f, 0.1f);
    lastStep_ = f.synced ? f.step : 0;
    lastUpdate_ = r.now;

    return dt;
}

void Pipeline::FillPath(
    SmoothingInput* in,
    PoseSource& poses,
    const FrameTimes& f,
    const OutputRequest& r,
    bool standIn,
    float dt
) {
    // The head's path around the frame (the look-ahead): sampled a display period apart around
    // the moment the frame stands for, with the pose of the picture shown at each moment: on the
    // moment's own slot while each frame shows its own picture; with an older picture standing in,
    // as far after the moment as the shown pictures run ahead (ShownLead), between slots.
    DrawnPose& drawnPose = *stages_.drawnPose;
    const double moment = drawnPose.MomentOf(f);
    const double period = poses.Period();
    int behind = 0, ahead = 0;
    stages_.smoothing->Window(period, &behind, &ahead);
    if (behind + ahead <= 0) {
        return;
    }

    const int n = behind + ahead + 1;
    if (int(times_.size()) < n) {
        times_.resize(n), heads_.resize(n), pictures_.resize(n), pictureTimes_.resize(n);
    }

    for (int i = 0; i < n; i++) {
        times_[i] = moment + (i - behind) * period;
    }
    poses.HeadsAt(times_.data(), n, heads_.data());
    const double lead = standIn ? ShownLead(drawnPose.MomentOf(*r.image) - moment, dt) : 0;
    if (lead == 0) {
        drawnPose.PosesAt(times_.data(), heads_.data(), n, poses, pictures_.data());
    } else {
        for (int i = 0; i < n; i++) {
            pictureTimes_[i] = times_[i] + lead;
        }
        drawnPose.PosesBetween(pictureTimes_.data(), heads_.data(), n, poses, pictures_.data());
    }

    in->heads = heads_.data();
    in->pathPictures = pictures_.data();
    in->period = period;
}

void Pipeline::Record(
    Observer& observer,
    const MirrorSettings& s,
    const FrameTimes& f,
    const OutputRequest& r,
    const FramePoses& poses,
    float edgeShiftDeg
) {
    const FrameTimes& sf = r.image ? *r.image : f;
    FrameRecord rec;
    rec.tick = r.tick > 0 ? r.tick : r.now;
    rec.draw = r.now;
    rec.serial = sf.serial;
    rec.arrival = sf.arrival, rec.smoothArrival = sf.smoothArrival, rec.slot = sf.shownSlot;
    rec.slotLead = sf.slotLead;
    rec.drawnSlot = sf.drawnSlot;

    rec.img = poses.image, rec.raw = poses.raw, rec.view = poses.view;
    rec.drawnPose = NameOf(config_.drawnPose);
    rec.zoom = s.zoom;
    rec.edgeClamps = stops_, rec.edgePlanned = planned_;
    rec.viewTime = r.image ? f.poseTime : 0;
    rec.followedSerial = f.serial;
    rec.edgeShiftDeg = edgeShiftDeg;
    rec.aspect = r.aspect;

    if (observer.WantsNotes()) {
        NotesReport notes(rec.pictureNotes, sizeof(rec.pictureNotes));
        stages_.drawnPose->Describe(sf, notes);
    }

    observer.Frame(rec);
}

double Pipeline::ShownLead(double lead, float dt) {
    // The median of the last kLeadKept ticks' leads: a repeated or skipped frame moves one tick's lead
    // by a period and doesn't count, and a lasting change (the frame picker fixing its delay) is in it
    // within half of them. Eased over ~0.1 s once that many are known, so the path's pictures don't
    // jump; until then the median itself (e2e: the first tick's frame a period off, eased from there,
    // stopped the view on the next 7 ticks, by up to 2.4 deg).
    leads_[leadCount_++ % kLeadKept] = lead;

    const int k = std::min(leadCount_, kLeadKept);
    double sorted[kLeadKept];
    std::copy(leads_, leads_ + k, sorted);
    std::nth_element(sorted, sorted + k / 2, sorted + k);

    constexpr double kEaseS = 0.1;
    if (leadCount_ <= kLeadKept) {
        shownLead_ = sorted[k / 2];
    } else {
        shownLead_ += (sorted[k / 2] - shownLead_) * std::min(1.0, dt / kEaseS);
    }

    return shownLead_;
}
