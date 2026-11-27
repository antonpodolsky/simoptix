// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "vr_mirror/frame_sources/demo/demo_source.h"

#include <timeapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "shared/log.h"
#include "shared/thread_cpu.h"

namespace {

    // A frame is handed over this long before its moment, as a game hands it to the headset ahead of the
    // display slot it was drawn for (the OpenXR layer's frames: 17 ms, median of one session). The head pose
    // at an output's own moment is then known, not extrapolated: handed over 1.4 ms ahead, Instant's view was
    // pinned to the image's edge on nearly every frame.
    constexpr double kLeadS = 0.017;
    constexpr double kFallenBehind = 0.25;  // s late: play on from now (the PC stalled)

    int64_t Counter() {
        LARGE_INTEGER c;
        QueryPerformanceCounter(&c);
        return c.QuadPart;
    }

    // The lap's first frame at or after `t` (s from the line), or its very first if none is.
    size_t FirstFrameFrom(const RecordedLap& lap, double t) {
        const auto first = std::lower_bound(
            lap.frames.begin(),
            lap.frames.end(),
            t,
            [](const RecordedLapFrame& f, double at) { return f.t < at; }
        );

        return first == lap.frames.end() ? 0 : static_cast<size_t>(first - lap.frames.begin());
    }

}

DemoSource::DemoSource() {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    frequency_ = static_cast<double>(f.QuadPart);
    frameEvent_.Reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    stop_.Reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
}

DemoSource::~DemoSource() {
    Stop();
    ReleaseImages();
}

bool DemoSource::Start(std::string* error) {
    if (!arrival_) {
        *error = "no arrival timing set (DemoSource::SetArrivalTiming)";
        return false;
    }

    Stop();
    std::wstring stem;

    if (!NewestRecordedLap(&stem)) {
        *error = "demo: no lap recorded yet (Record a lap, with the game running)";
        return false;
    }

    RecordedLap lap;

    if (!ReadRecordedLap(stem, &lap, error) || !DemoVideo::Check(stem + L".mp4", error)) {
        *error = "demo: " + *error;
        return false;
    }

    lap_ = std::move(lap);
    imageWidth_ = static_cast<uint32_t>(lap_.width);
    imageHeight_ = static_cast<uint32_t>(lap_.height);
    videoPath_ = stem + L".mp4";
    adapter_ = DemoVideo::DefaultAdapter();
    period_ = lap_.period;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        eye_ = lap_.eye;
        poses_.Clear();
        finishedCount_ = finishedHead_ = 0;
    }

    arrival_->Reset();
    stats_.Reset();
    lapStart_ = Now() + 0.05 - lap_.frames.front().t;  // its first frame in 50 ms
    next_ = 0;

    CoreLog(
        "demo: playing the newest recorded lap, %.1f s, %zu frames at %dx%d",
        lap_.length,
        lap_.frames.size(),
        lap_.width,
        lap_.height
    );

    ResetEvent(stop_.Get());
    running_ = true;

    thread_ = std::thread([this] {
        // An exception escaping a thread would end OBS on the spot; log it and stop playing.
        try {
            Run();
        } catch (...) {
            CoreLog("demo: the demo thread stopped by an unexpected error");
        }
    });

    return true;
}

void DemoSource::Stop() {
    running_ = false;
    SetEvent(stop_.Get());

    if (thread_.joinable()) {
        thread_.join();
    }
}

EyeInfo DemoSource::Eye() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return eye_;
}

double DemoSource::Now() const {
    return static_cast<double>(Counter()) / frequency_;
}

// ---- The demo thread. ----

// Wakes when each frame is due to be handed out, and otherwise sleeps.
void DemoSource::Run() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    timeBeginPeriod(1);

    while (running_) {
        wakeups_++;
        const double now = Now();

        while (HandOut(now)) {
        }

        const ArrivalStats::Report report = stats_.Tick(now);

        if (report.window) {
            fps_ = report.fps;
        }

        const double due = lapStart_ + lap_.frames[next_].t - kLeadS;
        const double waitS = std::clamp(due - Now(), 0.001, 0.05);
        WaitForSingleObject(stop_.Get(), static_cast<DWORD>(std::lround(waitS * 1000)));
    }

    timeEndPeriod(1);
}

// Hands the lap's next frame to the hosts once it is due, kLeadS before its moment. True if it did.
bool DemoSource::HandOut(double now) {
    double moment = lapStart_ + lap_.frames[next_].t;

    if (now < moment - kLeadS) {
        return false;
    }

    if (now > moment + kFallenBehind) {
        lapStart_ += now + kLeadS - moment;
        moment = now + kLeadS;
    }

    const Quat pose = lap_.frames[next_].pose;
    const double period = period_.load();
    const ArrivalResult a = arrival_->Add({now, 0, moment, period});

    Playing p;
    p.index = static_cast<int>(next_);
    p.frame.buffer = 0;
    p.frame.serial = ++serial_;
    p.frame.time = now;
    p.frame.smoothTime = a.smooth;
    p.frame.synced = true;
    p.frame.slotLead = a.slot - now;
    p.frame.slotTime = a.slot;
    p.frame.drawnSlot = moment;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        poses_.AddPose(moment, pose);
        poses_.AddPrediction(moment, pose);  // what the frame for this moment was drawn with
        finished_[(finishedHead_ + finishedCount_) % kFinished] = p;

        if (finishedCount_ < kFinished) {
            finishedCount_++;
        } else {
            finishedHead_ = (finishedHead_ + 1) % kFinished;
        }
    }

    framesArrived_++;
    stats_.Frame();
    stats_.Removed(now - a.smooth);
    SetEvent(frameEvent_.Get());

    if (++next_ == lap_.frames.size()) {
        // The lap ends on the line it started from: the next one plays on from its start.
        lapStart_ += lap_.length;
        next_ = 0;
        laps_++;
    }

    return true;
}

// ---- The hosts' side. ----

GameFrame DemoSource::LatestFrame() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (finishedCount_ == 0) {
        return {};
    }

    return finished_[(finishedHead_ + finishedCount_ - 1) % kFinished].frame;
}

bool DemoSource::NextFrame(uint64_t after, GameFrame* f) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (int i = 0; i < finishedCount_; i++) {
        const Playing& e = finished_[(finishedHead_ + i) % kFinished];

        if (e.frame.serial > after) {
            *f = e.frame;
            return true;
        }
    }

    return false;
}

bool DemoSource::FindFinished(uint64_t serial, Playing* out) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (int i = 0; i < finishedCount_; i++) {
        const Playing& e = finished_[(finishedHead_ + i) % kFinished];

        if (e.frame.serial == serial) {
            *out = e;
            return true;
        }
    }

    return false;
}

Quat DemoSource::HeadAt(double t) {
    std::lock_guard<std::mutex> lock(mutex_);
    return poses_.At(t);
}

void DemoSource::HeadsAt(const double* t, int n, Quat* out) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (int i = 0; i < n; i++) {
        out[i] = poses_.At(t[i]);
    }
}

bool DemoSource::PredictedHeadAt(double slot, Quat* q) {
    std::lock_guard<std::mutex> lock(mutex_);
    return poses_.PredictionFor(slot, 0.25 * period_.load(), q);
}

void DemoSource::PredictedHeadsAt(const double* slots, int n, Quat* out) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (int i = 0; i < n; i++) {
        out[i] = poses_.At(slots[i]);
    }

    poses_.PredictionsFor(slots, n, 0.25 * period_.load(), out);
}

void DemoSource::PredictedHeadsBetween(const double* t, int n, Quat* out) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (int i = 0; i < n; i++) {
        out[i] = poses_.At(t[i]);
    }

    poses_.PredictionsBetween(t, n, 1.5 * period_.load(), out);
}

// Opens the video on the host's device, once per device. False if it can't be decoded (logged once).
bool DemoSource::OpenVideo(ID3D11Device* device) {
    if (video_.IsOpen() && video_.Device() == device) {
        return true;
    }

    if (device == failedDevice_) {
        return false;  // logged once
    }

    Forget();
    std::string error;

    if (!video_.Open(videoPath_, lap_.width, lap_.height, device, &error)) {
        CoreLog("demo: %s", error.c_str());
        failedDevice_ = device;
        return false;
    }

    decodeNext_ = 0;
    return true;
}

// Lets go of the frames decoded and not copied.
void DemoSource::Forget() {
    for (int i = 0; i < queued_; i++) {
        queue_[i] = {};
    }

    queued_ = 0;
}

// Makes lap frame `index`'s image ready to copy: decoded, with kAhead frames decoded after it, and first in
// the queue. Frames the host never asked for are decoded and dropped. An earlier frame (the lap started
// again) or one far ahead (the host fell behind) seeks the video. False if the frame can't be decoded.
bool DemoSource::Ready(int index) {
    const int first = queued_ > 0 ? queue_[0].index : decodeNext_;
    const bool seek = index < first || index > first + kSeekPast;

    if (seek && !Seek(index)) {
        return false;
    }

    while (queued_ > 0 && queue_[0].index < index) {
        std::move(queue_ + 1, queue_ + queued_, queue_);
        queue_[--queued_] = {};
    }

    const int last = std::min(index + kAhead, static_cast<int>(lap_.frames.size()) - 1);

    while (decodeNext_ <= last) {
        Microsoft::WRL::ComPtr<IMFSample> sample;
        double t = 0;
        bool end = false;

        if (!video_.Next(&sample, &t, &end)) {
            if (!end) {
                CoreLog("demo: the lap's video could not be decoded");
            }

            break;
        }

        const int decoded = FrameAt(t);

        if (decoded < 0) {
            continue;  // no frame of the lap: dropped
        }

        if (decoded >= index && queued_ < kQueue) {
            queue_[queued_++] = {decoded, std::move(sample)};
        }

        decodeNext_ = decoded + 1;
    }

    return queued_ > 0 && queue_[0].index == index;
}

// The video on to the key frame before lap frame `index`: the frames from there are decoded on.
bool DemoSource::Seek(int index) {
    Forget();
    decodeNext_ = 0;
    const RecordedLapFrame& start = lap_.frames.front();

    // Half a period early: the key frame at the frame itself, not the next.
    if (!video_.SeekTo(lap_.frames[index].t - start.t - 0.5 * period_.load())) {
        CoreLog("demo: the lap's video could not be played from %.1f s", lap_.frames[index].t);
        return false;
    }

    return true;
}

// The lap frame whose image the video shows at `videoT` (s), or -1 if none. The video starts at the lap's
// first frame: its time in the video is the frame's moment less the first frame's.
int DemoSource::FrameAt(double videoT) const {
    const double t = videoT + lap_.frames.front().t;
    const double halfPeriod = 0.5 * period_.load();
    const size_t i = FirstFrameFrom(lap_, t - halfPeriod);

    if (std::fabs(lap_.frames[i].t - t) > halfPeriod) {
        return -1;
    }

    return static_cast<int>(i);
}

void DemoSource::OpenImages(ID3D11Device* device) {
    OpenVideo(device);
}

bool DemoSource::CopyImage(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    const GameFrame& f,
    FrameCopy* into,
    bool shared
) {
    Playing e;

    if (!FindFinished(f.serial, &e) || !OpenVideo(device) || !Ready(e.index)) {
        return false;
    }

    FrameCopyShape shape;
    shape.width = static_cast<UINT>(lap_.width), shape.height = static_cast<UINT>(lap_.height);
    shape.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    shape.shared = shared;
    into->Fit(device, shape);

    return into->Texture() && video_.CopyInto(queue_[0].sample.Get(), context, into->Texture());
}

void DemoSource::ReleaseImages() {
    Forget();
    video_.Close();
    failedDevice_ = nullptr;
}

uint64_t DemoSource::ThreadCycles() {
    return ::ThreadCycles(thread_);
}

uint64_t DemoSource::TakeStats(double seconds, void (*line)(const char*)) {
    const uint64_t wakeups = wakeups_.exchange(0), laps = laps_.exchange(0);
    const uint64_t arrived = framesArrived_.load(), frames = arrived - statArrived_;
    statArrived_ = arrived;

    if (!line) {
        return wakeups;
    }

    char text[160];

    snprintf(
        text,
        sizeof(text),
        "demo: %llu frames in %.0f s, the lap started again %llu time(s)",
        static_cast<unsigned long long>(frames),
        seconds,
        static_cast<unsigned long long>(laps)
    );

    line(text);
    return wakeups;
}
