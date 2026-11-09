// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "mirror_view.h"

#include <windows.h>
#include <graphics/vec4.h>
#include <util/platform.h>

#include <algorithm>
#include <cstring>

#include "pipeline/pipeline_builder.h"
#include "shared/local_files.h"
#include "shared/program_delay.h"
#include "shared/thread_cpu.h"
#include "warp_effect.h"

namespace {

    double Seconds(uint64_t ns) {
        return double(ns) * 1e-9;
    }

    // A line of the stages' shared report (StatsReport), in OBS's log.
    void LogLine(const char* line) {
        blog(LOG_INFO, "[simoptix_vr_mirror] %s", line);
    }

    void SetVec4(gs_effect_t* e, const char* name, const float* f) {
        vec4 v;
        vec4_set(&v, f[0], f[1], f[2], f[3]);
        gs_effect_set_vec4(gs_effect_get_param_by_name(e, name), &v);
    }

}

MirrorView::MirrorView(std::unique_ptr<FrameSource> frames, const MirrorSettings& settings, bool debug)
    : frameSource_(std::move(frames)) {
    FollowCanvas();
    SetSettings(settings, debug);
    frameSource_->SetArrivalTiming(  // before any Start
        MakeArrivalTiming(ConfigFor(settings_, OutputClock::kTicks, frameSource_->Report()).arrivalTiming)
    );

    obs_enter_graphics();
    char* errors = nullptr;
    effect_ = gs_effect_create(WarpEffectText().c_str(), "simoptix_vr_mirror_warp", &errors);
    if (!effect_) {
        blog(LOG_WARNING, "[simoptix_vr_mirror] effect failed: %s", errors ? errors : "?");
    }
    bfree(errors);
    obs_leave_graphics();
}

MirrorView::~MirrorView() {
    tickTrace_.Close();
    session_.StopNow();
    obs_enter_graphics();
    if (effect_) {
        gs_effect_destroy(effect_);
    }
    effect_ = nullptr;
    DestroyTextures();
    obs_leave_graphics();
}

void MirrorView::SetSettings(const MirrorSettings& settings, bool debug) {
    settings_ = settings;
    stamp_ = debug;
    trace_ = debug;
}

// OBS's graphics context entered.
void MirrorView::DestroyTextures() {
    for (gs_texture_t*& t : textures_) {
        if (t) {
            gs_texture_destroy(t);
        }
        t = nullptr;
    }
}

// Takes the canvas size from OBS's video settings (a plain copy, no lock): a change there applies
// at the next tick.
void MirrorView::FollowCanvas() {
    obs_video_info ovi = {};
    if (!obs_get_video_info(&ovi) || !ovi.base_width || !ovi.base_height) {
        return;
    }
    if (ovi.base_width != width_ || ovi.base_height != height_) {
        blog(LOG_INFO, "[simoptix_vr_mirror] size %ux%u (the canvas)", ovi.base_width, ovi.base_height);
    }

    width_ = ovi.base_width;
    height_ = ovi.base_height;
}

// Debug frame trace, for matching a recording (with the frame-number stamp) to the poses the
// warp used: trace_<time>_ticks.csv here (one row per drawn tick) and the frame source's own beside it,
// if it has one (Pimax: trace_<time>_pimax.csv, head poses, slot predictions, frame arrivals), in
// %LOCALAPPDATA%\simoptix\traces. LocalFiles names it and keeps the newest traces.
// Opens with the setting at the game's first frame, and closes with the setting or when the source stops.
// Graphics thread.
void MirrorView::ToggleTrace(bool on) {
    if (on == tickTrace_.IsOpen()) {
        return;
    }
    if (!on) {
        pipeline_.SetTraceSink(nullptr);  // its last row first
        tickTrace_.Close();
        frameSource_->StopTrace();
        blog(LOG_INFO, "[simoptix_vr_mirror] frame trace closed");
        return;
    }

    wchar_t prefix[MAX_PATH], path[MAX_PATH];

    if (!NewLocalFilePath(LocalFileKind::kFrameTrace, prefix, MAX_PATH)) {
        trace_ = false;
        blog(LOG_WARNING, "[simoptix_vr_mirror] no folder for the frame trace");
        return;
    }

    swprintf(path, MAX_PATH, L"%s_ticks.csv", prefix);

    // The pipeline's observer writes its header and rows (StampTraceObserver).
    if (!tickTrace_.Open(path, "")) {
        trace_ = false;  // don't retry every tick
        blog(LOG_WARNING, "[simoptix_vr_mirror] could not write the frame trace");
        return;
    }

    tickTraceSink_.writer = &tickTrace_;
    pipeline_.SetTraceSink(&tickTraceSink_);
    const bool own = frameSource_->StartTrace(prefix);

    // On this thread, timed: it lists one folder and deletes a trace or two.
    const uint64_t t0 = os_gettime_ns();
    const int deleted = PruneLocalFiles(LocalFileKind::kFrameTrace, 0);
    const double pruneMs = (os_gettime_ns() - t0) / 1e6;

    char utf8[MAX_PATH * 3];
    WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), nullptr, nullptr);
    blog(
        LOG_INFO,
        "[simoptix_vr_mirror] frame trace: %s%s; %d older trace(s) deleted in %.2f ms",
        utf8,
        own ? " (and the frame source's own beside it)" : "",
        deleted,
        pruneMs
    );
}

// The pipeline for the settings and what the frame source reports, with the frame-number stamp and the
// frame trace while Debug is on.
PipelineConfig MirrorView::ConfigOf() const {
    PipelineConfig c = ConfigFor(settings_, OutputClock::kTicks, frameSource_->Report());
    c.observer = trace_ || stamp_ ? ObserverKind::kStampTrace : ObserverKind::kNone;
    return c;
}

// Hands the pipeline's drawn pose to the ring's copy thread, which feeds it the frames' pictures,
// when it is a new one (its config value changed). Graphics thread.
void MirrorView::FeedDrawnPose() {
    const std::shared_ptr<DrawnPose>& p = pipeline_.SharedDrawnPose();
    if (p.get() == fedDrawnPose_) {
        return;
    }
    fedDrawnPose_ = p.get();
    ring_.SetDrawnPose(p);
    blog(LOG_INFO, "[simoptix_vr_mirror] drawn pose: %s", NameOf(pipeline_.Config().drawnPose));
}

// Starts measuring the output delay again (the source started, or the smoothing changed): a fresh
// pipeline, its frame picker restarted, the ring sized for the warm-up. Graphics thread.
void MirrorView::RestartDelay() {
    const PipelineConfig config = ConfigOf();
    delaySmoothing_ = int(config.smoothing);
    BuildPipeline(config, &pipeline_);
    FeedDrawnPose();
    FramePicker& picker = *pipeline_.Picker();
    picker.Restart(SmoothingDelayS(config.smoothing), FrameRing::kMaxSlots);
    outDelay_ = picker.OutDelay();
    ring_.SetSlotCount(picker.SlotsInUse());
    SetPictureNeed();
}

// Starts and stops the frame source as the source is shown and hidden. The session worker does
// the work on its own thread; the graphics thread only asks, sets up its own per-start state once
// the session runs, and frees its textures when hidden. Graphics thread, every tick.
void MirrorView::StartStop() {
    const SessionWorker::State state = session_.Get();
    if (state == SessionWorker::kStopped && session_.TakeFailure()) {
        wanted_ = false;  // retry when shown again
    }

    if (wanted_ && state == SessionWorker::kStopped) {
        RestartDelay();  // the ring sized for the warm-up before the first frame: it is made at this size
        session_.Start();
    } else if (wanted_ && state == SessionWorker::kRunning && !started_) {
        started_ = true;
        framesAtStart_ = frameSource_->FramesArrived();
        clock_.Reset();
        RestartDelay();  // measure the output delay again
        warpSlot_ = -1;
        statStart_ = 0;  // a new 10 s stretch of stats (LogStats)
        statTicks_ = statRepeats_ = statJumps_ = statNoFrame_ = statHeld_ = 0;
        blog(LOG_INFO, "[simoptix_vr_mirror] started (source shown)");
    } else if (!wanted_ && state == SessionWorker::kRunning) {
        const uint64_t t0 = os_gettime_ns();
        if (started_) {
            ToggleTrace(false);
            started_ = false;
            warpSlot_ = -1;
            obs_enter_graphics();
            DestroyTextures();
            obs_leave_graphics();
        }

        session_.Stop();
        SetProgramDelay(0);
        blog(
            LOG_INFO,
            "[simoptix_vr_mirror] stopping (source hidden): graphics thread done in %.2f ms",
            (os_gettime_ns() - t0) / 1e6
        );
    }
}

void MirrorView::Tick() {
    FollowCanvas();  // also while hidden: OBS lays the source out by its size
    StartStop();
    if (!started_) {
        return;
    }
    ToggleTrace(
        trace_ && frameSource_->FramesArrived() > framesAtStart_
    );  // no trace before the game's frames
    SetPictureNeed();
    const uint64_t q0 = os_gettime_ns();
    const double now = frameSource_->Now();
    const uint64_t q1 = os_gettime_ns();
    clock_.Sample(now, Seconds(q0), Seconds(q1));
}

// Picks the game frame for this tick and computes the warp. Once per OBS tick: several views
// (program, preview, projectors) may render the same tick.
void MirrorView::Prepare(uint64_t tickNs) {
    lastTickNs_ = tickNs;
    warpSlot_ = -1;
    if (!started_ || !clock_.Valid()) {
        return;
    }
    const double tick = Seconds(tickNs) + clock_.Offset();  // this tick on the frame source's clock
    ApplySettingChanges(ConfigOf());

    // The ring as the frame picker sees it (fixed arrays: nothing allocated per tick).
    ring_.Snapshot(snapshot_);
    for (int i = 0; i < FrameRing::kMaxSlots; i++) {
        const FrameRing::SlotInfo& s = snapshot_[i];
        stored_[i] = {
            s.valid,
            s.frame.serial,
            s.frame.time,
            s.frame.smoothTime,
            s.frame.slotTime,
            s.ready,
            s.frame.synced
        };
    }

    FramePicker& picker = *pipeline_.Picker();
    const Pick pick = picker.Tick(tick, frameSource_->Period(), stored_, FrameRing::kMaxSlots);

    if (picker.OutDelay() != outDelay_) {
        ApplyFixedDelay(picker);
    }

    SetProgramDelay(picker.OutDelay());
    switch (pick.kind) {
        case PickKind::kRepeat:
            statRepeats_++;
            break;
        case PickKind::kJump:
            statJumps_++;
            break;
        case PickKind::kHeld:
            statHeld_++;
            break;  // a copy running late: the last picture again
        case PickKind::kNone:
            statNoFrame_++;
            return;
        case PickKind::kNext:
            break;
    }

    const FrameRing::SlotInfo& si = snapshot_[pick.index];
    warpSlot_ = pick.index;

    OpenSlotTexture(si);

    // This tick's own timeline frame: its pose time advances by exactly one OBS frame per tick,
    // and the image is re-projected from its own pose to it.
    const FrameTimes tf = MomentTimes((1ull << 62) | ++ticks_, pick.moment);
    const float aspect = float(width_) / float(std::max<uint32_t>(1, height_));
    const FrameTimes image = TimesOf(si.frame);
    warp_ = pipeline_.Update(
        settings_,
        *frameSource_,
        frameSource_->Eye(),
        {tf, &image, frameSource_->Now(), aspect, tick}
    );

    statTicks_++;
}

void MirrorView::ApplySettingChanges(const PipelineConfig& config) {
    if (int(config.smoothing) != delaySmoothing_) {
        RestartDelay();
        blog(
            LOG_INFO,
            "[simoptix_vr_mirror] smoothing: %s",
            config.smoothing == SmoothingKind::kSmoothest ? "Smoothest"
            : config.smoothing == SmoothingKind::kSmooth  ? "Smooth"
                                                          : "Instant"
        );
    }

    if (Stabilizes(settings_) != loggedStabilize_) {
        // Off and Instant share the smoothing: it starts afresh, so Instant doesn't glide on from where
        // it was before Off.
        loggedStabilize_ = Stabilizes(settings_);
        pipeline_.Reset();
        blog(LOG_INFO, "[simoptix_vr_mirror] stabilization: %s", loggedStabilize_ ? "on" : "off");
    }

    ReconfigurePipeline(config, &pipeline_);  // e.g. the debug switch
    FeedDrawnPose();                          // the copy thread switches at its next pass
}

void MirrorView::ApplyFixedDelay(FramePicker& picker) {
    // The picker fixed its delay (with look-ahead smoothing each frame is shown that much later
    // still: its view needs the head's movement after it), and its own numbers for it.
    outDelay_ = picker.OutDelay();
    const double lookahead = SmoothingDelayS(pipeline_.Config().smoothing);
    blog(
        LOG_INFO,
        "[simoptix_vr_mirror] output delay %.1f ms (look-ahead %.0f ms)",
        outDelay_ * 1000,
        lookahead * 1000
    );

    LineStatsReport<void (*)(const char*)> log(LogLine);
    picker.Report(log);

    // The ring only needs the frames inside the delay, plus room for a late copy. Close
    // OBS's handles to the slots it frees.
    ring_.SetSlotCount(picker.SlotsInUse());
    for (int i = picker.SlotsInUse(); i < FrameRing::kMaxSlots; i++) {
        if (textures_[i]) {
            gs_texture_destroy(textures_[i]);
        }
        textures_[i] = nullptr;
    }
}

void MirrorView::OpenSlotTexture(const FrameRing::SlotInfo& slot) {
    // (Re)open the slot's shared texture on OBS's device.
    if (!textures_[warpSlot_] || textureGen_[warpSlot_] != slot.generation) {
        if (textures_[warpSlot_]) {
            gs_texture_destroy(textures_[warpSlot_]);
        }
        textures_[warpSlot_] = gs_texture_open_shared(uint32_t(reinterpret_cast<uintptr_t>(slot.shared)));
        textureGen_[warpSlot_] = slot.generation;
    }
}

// How large the canvas shows the pictures: a frame source that can scale them (the OpenXR layer's) keeps
// them that size, within the memory allowed for the ring's slots. Graphics thread: before the frame source
// starts, and every tick while it runs (the canvas, the zoom, the slots and the game's field of view can
// change).
void MirrorView::SetPictureNeed() {
    const float aspect = float(width_) / float(std::max<uint32_t>(1, height_));
    frameSource_->SetPictureNeed(
        NativePictureWidth(frameSource_->Eye(), settings_, int(width_), aspect),
        kPictureMemory / ring_.SlotCount()
    );
}

// Starts a stretch of footprint stats: the threads' cycle counts so far, and what was counted
// before is dropped. Graphics thread.
void MirrorView::StartFootprint() {
    footClock_ = CpuClock();
    footPoll_ = frameSource_->ThreadCycles();
    footCopy_ = ring_.ThreadCycles();
    frameSource_->TakeStats(0, nullptr);
    ring_.TakeWakeups();
    prepareTimes_.Reset();
}

// The footprint of the last `seconds`: CPU and wake-ups of this source's threads, the graphics
// thread's time in Prepare per tick, and the frame source's own lines (Pimax: every call into the
// runtime by kind). (The Overlays plugin logs its LMU telemetry reader's own.) Graphics thread, every
// 10 s.
void MirrorView::LogFootprint(double seconds) {
    const uint64_t clockCycles = CpuClock() - footClock_;
    const double poll = CpuPercent(frameSource_->ThreadCycles(), footPoll_, clockCycles);
    const double copy = CpuPercent(ring_.ThreadCycles(), footCopy_, clockCycles);
    const uint64_t sourceWakeups = frameSource_->TakeStats(seconds, LogLine);
    const int copyWakeups = ring_.TakeWakeups();
    const int ticks = prepareTimes_.Count();
    const double prepareMax = prepareTimes_.Longest(), prepareP99 = prepareTimes_.Quantile(0.99);
    blog(
        LOG_INFO,
        "[simoptix_vr_mirror] footprint: %s %.2f%% CPU (of one core), %.0f wake-ups/s; copy thread "
        "%.2f%%, %.0f wake-ups/s; graphics thread in Prepare p99 %.3f ms, max %.3f ms (%d ticks)",
        frameSource_->ThreadName(),
        poll,
        sourceWakeups / seconds,
        copy,
        copyWakeups / seconds,
        prepareP99 * 1000,
        prepareMax * 1000,
        ticks
    );
}

// The 10 s stats, while the source runs (also while no game frame arrives). Graphics thread, once
// per tick, after Prepare.
void MirrorView::LogStats(uint64_t tickNs) {
    if (!started_ || !clock_.Valid()) {
        return;
    }
    const double tick = Seconds(tickNs) + clock_.Offset();  // this tick on the frame source's clock
    if (statStart_ == 0) {
        statStart_ = tick;
        statFrames_ = frameSource_->FramesArrived();
        StartFootprint();
        return;
    }

    if (tick - statStart_ >= 10) {
        // The game's rate over this same stretch (SourceFps covers only the last 3 s: a stretch with a
        // stall in its first seconds would read 90 fps beside hundreds of repeats).
        const uint64_t frames = frameSource_->FramesArrived();

        // Every stage's own numbers (the drawn pose's: frames judged and re-timed, corrections), and
        // the copy thread's time looking at pictures with patches.
        {
            LineStatsReport<void (*)(const char*)> log(LogLine);
            pipeline_.Report(log);
            DrawnPoseFeed::ReportLookTime(ring_.TakeLookTime(), log);
        }

        blog(
            LOG_INFO,
            "[simoptix_vr_mirror] %d ticks in %.0f s: %d showed the same game frame again, %d skipped "
            "ahead, "
            "%d held the last image (copy late), %d without a frame; game at %.1f fps",
            statTicks_,
            tick - statStart_,
            statRepeats_,
            statJumps_,
            statHeld_,
            statNoFrame_,
            double(frames - statFrames_) / (tick - statStart_)
        );

        LogFootprint(tick - statStart_);
        statTicks_ = statRepeats_ = statJumps_ = statNoFrame_ = statHeld_ = 0;
        statStart_ = tick;
        statFrames_ = frames;
        StartFootprint();
    }
}

void MirrorView::DrawStamp() {
    gs_effect_t* solid = obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_eparam_t* color = gs_effect_get_param_by_name(solid, "color");
    for (int b = 0; b < 16; b++) {
        const bool on = b < 15 ? ((pipeline_.Stamp() >> b) & 1) != 0 : false;
        vec4 c;
        vec4_set(&c, on ? 1.0f : 0.0f, on ? 1.0f : 0.0f, on ? 1.0f : 0.0f, 1.0f);
        gs_effect_set_vec4(color, &c);
        gs_matrix_push();
        gs_matrix_translate3f(float(b * 16), 0.0f, 0.0f);
        while (gs_effect_loop(solid, "Solid")) {
            gs_draw_sprite(nullptr, 0, 16, 16);
        }
        gs_matrix_pop();
    }
}

void MirrorView::Render() {
    const uint64_t tickNs = obs_get_video_frame_time();
    if (tickNs != lastTickNs_) {
        const uint64_t t0 = os_gettime_ns();
        Prepare(tickNs);
        if (started_) {
            prepareTimes_.Add(Seconds(os_gettime_ns() - t0));
        }
        LogStats(tickNs);
    }

    if (!effect_ || warpSlot_ < 0 || !textures_[warpSlot_]) {
        return;
    }

    const WarpParams& p = warp_;
    SetVec4(effect_, "m0", p.m[0]);
    SetVec4(effect_, "m1", p.m[1]);
    SetVec4(effect_, "m2", p.m[2]);
    SetVec4(effect_, "outTan", p.outTan);
    SetVec4(effect_, "srcTan", p.srcTan);
    SetVec4(effect_, "rect", p.rect);
    SetVec4(effect_, "options", p.options);

    gs_texture_t* tex = textures_[warpSlot_];
    const float w = float(gs_texture_get_width(tex)), h = float(gs_texture_get_height(tex));
    const float size[4] = {w, h, 1.0f / std::max(1.0f, w), 1.0f / std::max(1.0f, h)};
    SetVec4(effect_, "texSize", size);
    gs_effect_set_texture(gs_effect_get_param_by_name(effect_, "image"), tex);

    while (gs_effect_loop(effect_, "Draw")) {
        gs_draw_sprite(textures_[warpSlot_], 0, width_, height_);
    }
    if (stamp_) {
        DrawStamp();
    }
}
