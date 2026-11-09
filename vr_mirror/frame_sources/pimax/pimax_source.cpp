// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "vr_mirror/frame_sources/pimax/pimax_source.h"

#include <timeapi.h>

#include <cmath>
#include <cstdio>

#include "shared/log.h"
#include "shared/thread_cpu.h"

namespace {

    double QpcSeconds() {
        static const double freq = [] {
            LARGE_INTEGER f;
            QueryPerformanceFrequency(&f);
            return double(f.QuadPart);
        }();

        LARGE_INTEGER c;
        QueryPerformanceCounter(&c);
        return double(c.QuadPart) / freq;
    }

    void MergeFootprint(PvrFootprint* into, const PvrFootprint& f) {
        for (int k = 0; k < kPvrCallKinds; k++) {
            into->calls[k].Merge(f.calls[k]);
        }
        into->wakeups += f.wakeups;
    }

    // Idle back-off: after kIdleAfter without a frame (none since Start, or the game quit), stop
    // the pose, clock and slot calls (about 2,500 a second into pi_server) and only wait for the
    // frame signal, kIdleWaitMs at a time (PvrMirror::WaitForFrame). The first frame wakes
    // the wait at once; full rate from then on.
    constexpr double kIdleAfter = 2.0;  // seconds without a frame
    constexpr DWORD kIdleWaitMs = 50;   // longest wait per idle pass: ~20 wake-ups a second

}

const char* PvrCallName(int kind) {
    static const char* const kNames[kPvrCallKinds] = {
        "clock",
        "pose",
        "slot",
        "prediction",
        "status",
        "config",
        "now",
    };

    return kind >= 0 && kind < kPvrCallKinds ? kNames[kind] : "?";
}

PimaxSource::~PimaxSource() {
    Stop();
    StopTrace();
}

bool PimaxSource::StartTrace(const wchar_t* prefix) {
    return trace_.Open(
        (std::wstring(prefix) + L"_pimax.csv").c_str(),
        "# times in source seconds; quaternions x,y,z,w\n"
        "# P,time,qx,qy,qz,qw: head pose sample\n"
        "# S,time,slot,qx,qy,qz,qw: pose predicted at `time` for display slot `slot`\n"
        "# F,serial,arrival,smooth,display_slot,current_slot,offset: frame arrival\n"
    );
}

void PimaxSource::StopTrace() {
    trace_.Close();
}

bool PimaxSource::Start(std::string* error) {
    if (!arrival_) {
        *error = "no arrival timing set (PimaxSource::SetArrivalTiming)";
        return false;
    }

    if (!session_.Open(error)) {
        return false;
    }
    poses_.SetPeriod(session_.Period());
    {
        PvrSession::Calls calls(session_);
        mirror_.Enable(calls, true);
    }

    Sleep(300);  // let the server create its buffer

    idle_ = false;
    running_ = true;
    clock_.Reset();
    clockValid_ = false;
    thread_ = std::thread([this] {
        // An exception escaping a thread would end OBS on the spot; log it and stop polling.
        try {
            PollThread();
        } catch (...) {
            CoreLog("Pimax: the poll thread stopped by an unexpected error");
        }
    });

    return true;
}

void PimaxSource::Stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
    {
        PvrSession::Calls calls(session_);
        mirror_.CloseSignal(calls);
        if (calls.HasSession()) {
            mirror_.Enable(calls, false);
        }
    }

    mirror_.ReleaseTexture();
    StopTrace();
    session_.Close();
}

bool PimaxSource::OpenTextures(ID3D11Device* device) {
    CallStats config;
    const bool ok = mirror_.OpenTexture(device, session_, config);
    AddFootprint(kCallConfig, config);
    return ok;
}

void PimaxSource::AddFootprint(int kind, const CallStats& stats) {
    std::lock_guard<std::mutex> state(mutex_);
    footprint_.calls[kind].Merge(stats);
}

struct PimaxSource::Poll {
    double lastEdgeSeen = 0;
    ArrivalStats stats;
    double lastPoseSample = 0, lastStatusCheck = 0;
    double lastFrameOrStart = 0;  // source time of the last frame, or of the first pass
    double idleSince = 0;         // performance-counter time idle began
    bool edgePending = false;     // the idle wait consumed an auto-reset frame signal: a frame
    double period = 0;

    // Runtime calls and passes counted since they were last published to footprint_ (under the
    // state lock, once per pass: the calls themselves run under the call lock only).
    PvrFootprint pending;
};

struct PimaxSource::PassCalls {
    double now = 0;
    Quat pose, slotPose;
    bool tracked = false, predicted = false, haveSignal = false;
    double gridSlot = 0;
    bool ready = false;
};

void PimaxSource::PollThread() {
    timeBeginPeriod(1);

    // The frame signal is off for only ~1.5 ms per frame (while the next frame is written), and
    // Sleep(1) often oversleeps that, which skipped about 1-2 frames per second. Check the
    // signal every 0.25 ms with a high-resolution timer; sample poses at 1 ms as before.
    UniqueHandle timer(
        CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS)
    );

    Poll poll;
    poll.period = session_.Period();

    while (running_) {
        if (idle_) {
            if (WaitIdle(poll)) {
                continue;
            }
        } else {
            PollPass(poll);
        }

        CheckQuit(poll);
        WaitForNextPass(timer);
    }

    PublishFootprint(poll.pending);
    mirror_.EndWait();
    timeEndPeriod(1);
}

void PimaxSource::PublishFootprint(PvrFootprint& pending) {
    std::lock_guard<std::mutex> state(mutex_);
    MergeFootprint(&footprint_, pending);
    pending = {};
}

bool PimaxSource::WaitIdle(Poll& poll) {
    poll.pending.wakeups++;
    PublishFootprint(poll.pending);

    if (!mirror_.WaitForFrame(session_, kIdleWaitMs, &poll.edgePending)) {
        return false;
    }

    idle_ = false;
    CoreLog("game frames again after %.1f s idle: back to full rate\n", QpcSeconds() - poll.idleSince);

    // Timing starts again: the clock, pose and slot right away, a new frame-rate window.
    poll.lastPoseSample = 0;
    poll.stats.Restart();
    return true;
}

void PimaxSource::PollPass(Poll& poll) {
    poll.pending.wakeups++;

    const PassCalls pass = CallRuntime(poll);

    PublishPass(poll, pass);
    CheckRate(poll, pass.now, pass.haveSignal);
}

PimaxSource::PassCalls PimaxSource::CallRuntime(Poll& poll) {
    PassCalls pass;

    // The runtime's clock is a call into the Pimax server; ask it once per pose sample
    // (1 ms) and run on the performance counter plus the measured offset in between.
    pass.now = clockValid_.load() ? QpcSeconds() + clockOffset_.load() : 0;
    const bool sample = !clockValid_.load() || pass.now - poll.lastPoseSample >= 0.001;

    pass.ready = poll.edgePending;
    poll.edgePending = false;

    // Every runtime call of this pass, under the call lock only: the hosts'
    // threads read frames and poses under the state lock and never wait for pi_server.
    PvrSession::Calls calls(session_);

    if (sample) {
        double q0 = 0, q1 = 0;
        pass.now = calls.Time(&q0, &q1);
        clock_.Sample(pass.now, q0, q1);
        poll.pending.calls[kCallClock].Add(q1 - q0);

        if (clock_.Valid()) {
            clockOffset_ = clock_.Offset();
            clockValid_ = true;
        }

        poll.lastPoseSample = pass.now;
        pass.tracked = calls.Orientation(pass.now, &pass.pose, poll.pending.calls[kCallPose]);

        // The headset's display slots: a clean 90 Hz grid, used as a clock to
        // de-jitter arrivals.
        pass.gridSlot = calls.NextDisplaySlot(poll.pending.calls[kCallSlot]);
    }

    // The moment the runtime moves on to a new display slot is when a game starts its
    // frame for that slot and asks for the head pose predicted for it. Ask the same, now,
    // and keep the answer: it is the pose that frame will be rendered with.
    if (pass.gridSlot > lastPredictedSlot_ + 0.5 * poll.period) {
        lastPredictedSlot_ = pass.gridSlot;
        pass.predicted = calls.Orientation(
            pass.gridSlot,
            &pass.slotPose,
            poll.pending.calls[kCallPrediction]
        );
    }

    pass.haveSignal = mirror_.HasSignal(calls);

    if (!pass.ready) {
        pass.ready = mirror_.NewFrame(calls);
    }

    return pass;
}

void PimaxSource::PublishPass(Poll& poll, const PassCalls& pass) {
    const double now = pass.now;

    if (pass.gridSlot > 0) {
        currentSlot_ = pass.gridSlot;
    }

    if (pass.tracked && trace_.IsOpen()) {
        trace_.Printf(
            "P,%.6f,%.7f,%.7f,%.7f,%.7f\n",
            now,
            pass.pose.x,
            pass.pose.y,
            pass.pose.z,
            pass.pose.w
        );
    }

    if (pass.predicted && trace_.IsOpen()) {
        trace_.Printf(
            "S,%.6f,%.6f,%.7f,%.7f,%.7f,%.7f\n",
            now,
            pass.gridSlot,
            pass.slotPose.x,
            pass.slotPose.y,
            pass.slotPose.z,
            pass.slotPose.w
        );
    }

    ArrivalResult timing;
    timing.smooth = now;

    if (pass.ready) {
        if (currentSlot_ > 0) {
            timing = arrival_->Add({now, currentSlot_, 0, poll.period});
            poll.stats.Removed(now - timing.smooth);
        }

        poll.lastEdgeSeen = poll.lastFrameOrStart = now;
        poll.stats.Frame();
        framesArrived_++;
    }

    poses_.Add(pass.tracked, now, pass.pose, pass.predicted, pass.gridSlot, pass.slotPose);

    uint64_t serial = 0;
    {
        std::lock_guard<std::mutex> state(mutex_);
        if (pass.ready) {
            frame_ = {
                0,
                frame_.serial + 1,
                now,
                timing.smooth,
                true,
                currentSlot_ > 0 ? currentSlot_ - now : 0,
                timing.slot
            };
        } else if (now - poll.lastEdgeSeen > 0.5) {
            // No edges for a while: fall back to buffer 0 at the current time.
            frame_ = {mirror_.HasTexture() ? 0 : -1, frame_.serial + 1, now, now, false};
        }

        serial = frame_.serial;
        MergeFootprint(&footprint_, poll.pending);
        poll.pending = {};
    }

    if (pass.ready) {
        if (trace_.IsOpen()) {
            trace_.Printf(
                "F,%llu,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                static_cast<unsigned long long>(serial),
                now,
                timing.smooth,
                timing.slot,
                currentSlot_,
                timing.offset
            );
        }

        SetEvent(frameEvent_.Get());
    }
}

void PimaxSource::CheckRate(Poll& poll, double now, bool haveSignal) {
    const ArrivalStats::Report report = poll.stats.Tick(now);

    if (report.window) {
        fps_ = report.fps;
    }

    if (report.lowRate) {
        CoreLog(
            "low mirror frame rate: %d frames in %.1fs (headset idle, or the game dropping "
            "frames?)\n",
            report.frames,
            report.seconds
        );
    }

    if (report.noise) {
        CoreLog("arrival timing noise removed: %.2f ms rms\n", report.noiseMs);
    }

    // Idle only with a frame signal to wait on; without one, the unsynced frames above keep
    // the hosts drawing whatever the mirror holds.
    if (poll.lastFrameOrStart == 0) {
        poll.lastFrameOrStart = now;
    }

    if (haveSignal && now - poll.lastFrameOrStart > kIdleAfter) {
        idle_ = true;
        poll.idleSince = QpcSeconds();
        fps_ = 0;
        arrival_->Reset();  // the next game's frames find their own offset
        CoreLog(
            "no game frames for %.0f s: idle (no pose or clock calls, waiting for the frame "
            "signal)\n",
            kIdleAfter
        );
    }
}

void PimaxSource::CheckQuit(Poll& poll) {
    // The server asks the active VR app's session to close when another app takes over (our
    // session is the active one when it started before the game), and ends the process 10 s
    // later if it doesn't. So close it and come back as a secondary session.
    if (QpcSeconds() - poll.lastStatusCheck <= 0.25) {
        return;
    }

    poll.lastStatusCheck = QpcSeconds();

    bool quit = false;
    {
        PvrSession::Calls calls(session_);
        quit = calls.ShouldQuit(poll.pending.calls[kCallStatus]);
    }

    if (!quit) {
        return;
    }

    Reconnect();

    // A new session may count time from a new origin: start the clock offset,
    // frame-rate window, de-jitter offsets and pose history again.
    clock_.Reset();
    clockValid_ = false;
    poll.lastPoseSample = poll.lastEdgeSeen = poll.lastFrameOrStart = 0;
    idle_ = false;
    poll.edgePending = false;
    poll.stats.Reset();
    arrival_->Reset();

    {
        PvrSession::Calls calls(session_);
        mirror_.ResetEdge(calls);
    }

    currentSlot_ = 0;
    lastPredictedSlot_ = 0;
    poses_.Clear();
}

void PimaxSource::WaitForNextPass(const UniqueHandle& timer) {
    if (idle_) {
        // the idle pass has waited already
    } else if (timer) {
        LARGE_INTEGER due;
        due.QuadPart = -2500;  // 0.25 ms, relative, in 100 ns units
        SetWaitableTimer(timer.Get(), &due, 0, nullptr, nullptr, FALSE);
        WaitForSingleObject(timer.Get(), 5);
    } else {
        Sleep(1);
    }
}

// Closes the session at the server's request and opens a new one once the other app has taken
// over. The old mirror buffer stays usable to the hosts meanwhile; OpenTextures picks up the new
// one. Runs on the poll thread (no frames arrive meanwhile).
void PimaxSource::Reconnect() {
    CoreLog("the Pimax server asked this session to close (another VR app took over): reconnecting\n");
    {
        PvrSession::Calls calls(session_);
        mirror_.Enable(calls, false);
        mirror_.CloseSignal(calls);
    }

    session_.Recreate(running_, [&](PvrSession::Calls& calls) { mirror_.Enable(calls, true); });
    if (!running_) {
        return;
    }
    Sleep(300);  // let the server create its buffer
    CallStats config;
    mirror_.OpenSignal(session_, config);
    AddFootprint(kCallConfig, config);
    mirror_.ReopenOnNextOpen();
    CoreLog("reconnected to the Pimax server\n");
}

GameFrame PimaxSource::LatestFrame() {
    std::lock_guard<std::mutex> lock(mutex_);
    return frame_;
}

bool PimaxSource::NextFrame(uint64_t after, GameFrame* f) {
    const GameFrame latest = LatestFrame();
    if (!latest.synced || latest.buffer < 0 || latest.serial == after) {
        return false;
    }
    *f = latest;
    return true;
}

bool PimaxSource::CopyImage(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    const GameFrame& f,
    FrameCopy* into,
    bool shared
) {
    ID3D11Texture2D* src = f.buffer >= 0 ? mirror_.Texture() : nullptr;
    if (!src) {
        return false;
    }
    D3D11_TEXTURE2D_DESC sd;
    src->GetDesc(&sd);

    FrameCopyShape shape;
    shape.width = sd.Width, shape.height = sd.Height, shape.format = sd.Format;
    shape.shared = shared;
    into->Fit(device, shape);
    if (!into->Texture()) {
        return false;
    }
    context->CopyResource(into->Texture(), src);
    return true;
}

uint64_t PimaxSource::TakeStats(double seconds, void (*line)(const char*)) {
    int found = 0, missing = 0;
    TakePredictionStats(&found, &missing);
    const PvrFootprint f = TakeFootprint();
    if (!line) {
        return f.wakeups;
    }
    char text[160];
    snprintf(
        text,
        sizeof(text),
        "predicted poses: %d found, %d missing (those used the measured pose)",
        found,
        missing
    );

    line(text);
    std::string calls;
    uint64_t count = 0;
    double total = 0;
    for (int k = 0; k < kPvrCallKinds; k++) {
        AppendCallStats(&calls, PvrCallName(k), f.calls[k], seconds);
        count += f.calls[k].count;
        total += f.calls[k].total;
    }

    std::string all(calls.size() + 96, '\0');
    snprintf(
        &all[0],
        all.size(),
        "Pimax calls: %.0f/s, %.1f ms in %.0f s: %s",
        count / seconds,
        total * 1000,
        seconds,
        calls.c_str()
    );

    line(all.c_str());
    return f.wakeups;
}

double PimaxSource::Now() const {
    if (clockValid_.load()) {
        return QpcSeconds() + clockOffset_.load();
    }

    // Before the poll thread's first measurement (the first millisecond after Start): ask the
    // runtime, under the call lock like every other runtime call.
    CallStats now;
    const double t = session_.Time(now);
    std::lock_guard<std::mutex> state(mutex_);
    footprint_.calls[kCallNow].Merge(now);
    return t;
}

PvrFootprint PimaxSource::TakeFootprint() {
    std::lock_guard<std::mutex> lock(mutex_);
    const PvrFootprint f = footprint_;
    footprint_ = {};
    return f;
}

uint64_t PimaxSource::PollThreadCycles() {
    return ::ThreadCycles(thread_);
}
