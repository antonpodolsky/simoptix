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

    constexpr double kDecodeAhead = 0.003;  // s before a frame's moment: decoding takes 1 to 4 ms
    constexpr double kFallenBehind = 0.25;  // s late: play on from now (the PC stalled)

    int64_t Counter() {
        LARGE_INTEGER c;
        QueryPerformanceCounter(&c);
        return c.QuadPart;
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

    if (!ReadRecordedLap(stem, &lap, error) || !video_.Open(stem + L".mp4", lap.width, lap.height, error)) {
        *error = "demo: " + *error;
        return false;
    }

    lap_ = std::move(lap);
    period_ = lap_.period;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        eye_ = lap_.eye;
        poses_.Clear();
        finishedCount_ = finishedHead_ = 0;
    }

    for (std::atomic<uint64_t>& s : textureSerial_) {
        s = 0;
    }

    arrival_->Reset();
    stats_.Reset();
    lapStart_ = Now() + 0.05 - lap_.frames.front().t;  // its first frame in 50 ms
    next_ = 0;
    nextTexture_ = 0;
    decoding_ = false;

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

    video_.Close();
}

EyeInfo DemoSource::Eye() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return eye_;
}

double DemoSource::Now() const {
    return static_cast<double>(Counter()) / frequency_;
}

// ---- The demo thread. ----

// Wakes a few milliseconds before each frame's moment to decode it, then every millisecond until its image
// is ready, and otherwise sleeps.
void DemoSource::Run() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    timeBeginPeriod(1);

    while (running_) {
        wakeups_++;
        const double now = Now();

        if (decoding_ && video_.Done(decoded_.texture)) {
            Publish(now);
        }

        if (!decoding_ && !Decode(now)) {
            break;
        }

        const ArrivalStats::Report report = stats_.Tick(now);

        if (report.window) {
            fps_ = report.fps;
        }

        const double due = lapStart_ + lap_.frames[next_].t - kDecodeAhead;
        const double waitS = decoding_ ? 0.001 : std::clamp(due - Now(), 0.001, 0.05);
        WaitForSingleObject(stop_.Get(), static_cast<DWORD>(std::lround(waitS * 1000)));
    }

    timeEndPeriod(1);
}

// Decodes the lap's next frame once its moment is near. False if the video can't be decoded.
bool DemoSource::Decode(double now) {
    double due = lapStart_ + lap_.frames[next_].t;

    if (now < due - kDecodeAhead) {
        return true;
    }

    if (now > due + kFallenBehind) {
        lapStart_ += now - due;
        due = now;
    }

    const int texture = nextTexture_;
    textureSerial_[texture] = 0;  // being written: no host copies it now
    bool end = false;

    if (!video_.DecodeNext(texture, &end)) {
        if (!end) {
            CoreLog("demo: the lap's video could not be decoded");
            return false;
        }

        // The video ended before the poses: start the next lap.
        next_ = lap_.frames.size() - 1;
    } else {
        const Quat pose = lap_.frames[next_].pose;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            poses_.AddPose(due, pose);
            poses_.AddPrediction(due, pose);  // what the frame for this moment was drawn with
        }

        decoded_ = {};
        decoded_.frame.buffer = texture;
        decoded_.frame.drawnSlot = due;
        decoded_.texture = texture;
        decoding_ = true;
        nextTexture_ = (nextTexture_ + 1) % DemoVideo::kTextures;
    }

    if (++next_ == lap_.frames.size()) {
        // The lap ends on the line it started from: the next one plays on from its start.
        lapStart_ += lap_.length;
        next_ = 0;
        laps_++;

        if (!video_.Rewind()) {
            CoreLog("demo: the lap's video could not be played again from its start");
            return false;
        }
    }

    return true;
}

// Hands the decoded frame to the hosts: its image is ready.
void DemoSource::Publish(double now) {
    decoding_ = false;
    const double period = period_.load();
    const ArrivalResult a = arrival_->Add({now, 0, decoded_.frame.drawnSlot, period});

    GameFrame& f = decoded_.frame;
    f.serial = ++serial_;
    f.time = now;
    f.smoothTime = a.smooth;
    f.synced = true;
    f.slotLead = a.slot - now;
    f.slotTime = a.slot;
    textureSerial_[decoded_.texture] = f.serial;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        finished_[(finishedHead_ + finishedCount_) % kFinished] = decoded_;

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

void DemoSource::OpenImages(ID3D11Device* device) {
    if (device == hostDevice_ && hostTextures_[0]) {
        return;
    }

    ReleaseImages();
    hostDevice_ = device;

    for (int i = 0; i < DemoVideo::kTextures; i++) {
        if (const HANDLE shared = video_.Shared(i)) {
            device->OpenSharedResource(shared, IID_PPV_ARGS(&hostTextures_[i]));
        }
    }
}

bool DemoSource::CopyImage(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    const GameFrame& f,
    FrameCopy* into,
    bool shared
) {
    Playing e;

    if (!FindFinished(f.serial, &e) || textureSerial_[e.texture] != f.serial) {
        return false;
    }

    OpenImages(device);
    ID3D11Texture2D* image = hostTextures_[e.texture].Get();

    if (!image) {
        return false;
    }

    FrameCopyShape shape;
    shape.width = static_cast<UINT>(lap_.width), shape.height = static_cast<UINT>(lap_.height);
    shape.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    shape.shared = shared;
    into->Fit(device, shape);

    if (!into->Texture()) {
        return false;
    }

    context->CopyResource(into->Texture(), image);

    // Decoded over meanwhile (eight frames later): the copy may hold a later image.
    return textureSerial_[e.texture] == f.serial;
}

void DemoSource::ReleaseImages() {
    for (Microsoft::WRL::ComPtr<ID3D11Texture2D>& t : hostTextures_) {
        t.Reset();
    }

    hostDevice_ = nullptr;
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
