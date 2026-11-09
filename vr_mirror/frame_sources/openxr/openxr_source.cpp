// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "vr_mirror/frame_sources/openxr/openxr_source.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "shared/log.h"
#include "shared/thread_cpu.h"

namespace {

    constexpr double kIdleAfter = 2.0;  // seconds without a finished frame

    int64_t Counter() {
        LARGE_INTEGER c;
        QueryPerformanceCounter(&c);
        return c.QuadPart;
    }

    Quat QuatOf(const openxr_share::Pose& p) {
        return Normalize(Quat{p.qx, p.qy, p.qz, p.qw});
    }

}

OpenXrSource::OpenXrSource() {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    frequency_ = double(f.QuadPart);
    frameEvent_.Reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    stop_.Reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    session_event_.Reset(CreateEventW(nullptr, TRUE, FALSE, openxr_share::kSessionEvent));
}

OpenXrSource::~OpenXrSource() {
    Stop();
    StopTrace();
    pictures_.Release();
}

bool OpenXrSource::StartTrace(const wchar_t* prefix) {
    return trace_.Open(
        (std::wstring(prefix) + L"_openxr.csv").c_str(),
        "# times in source seconds; threads are Windows thread ids of the game\n"
        "# F,layer_frame,ended,drawn_slot,texture,game_image,images_held,released_during_end,end_thread,"
        "release_thread: a frame the layer published\n"
        "# A,layer_frame,serial,arrival: its copy seen finished, handed to the hosts as game frame `serial`\n"
    );
}

void OpenXrSource::StopTrace() {
    trace_.Close();
}

bool OpenXrSource::Start(std::string* error) {
    if (!arrival_) {
        *error = "no arrival timing set (OpenXrSource::SetArrivalTiming)";
        return false;
    }

    Stop();
    ResetEvent(stop_.Get());
    running_ = true;
    thread_ = std::thread([this] {
        try {
            Run();
        } catch (...) {
            CoreLog("OpenXR: the reader thread stopped by an unexpected error");
        }
    });

    return true;
}

void OpenXrSource::Stop() {
    running_ = false;
    if (stop_) {
        SetEvent(stop_.Get());
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    CloseShare(nullptr);
}

double OpenXrSource::Now() const {
    return double(Counter()) / frequency_;
}

LUID OpenXrSource::AdapterLuid() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return adapter_;
}

EyeInfo OpenXrSource::Eye() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return eye_;
}

double OpenXrSource::SourceSeconds(int64_t xrTime) const {
    return double(xrTime) * 1e-9 + clockOffset_;
}

// ---- The reader thread. ----

bool OpenXrSource::OpenShare() {
    mapping_.Reset(OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, openxr_share::kMappingName));
    if (!mapping_) {
        return false;
    }
    header_.Reset(
        static_cast<openxr_share::Header*>(MapViewOfFile(mapping_.Get(), FILE_MAP_ALL_ACCESS, 0, 0, 0))
    );

    if (!header_ || header_->magic != openxr_share::kMagic || header_->version != openxr_share::kVersion
        || header_->size != sizeof(openxr_share::Header) || header_->state.load() != openxr_share::kRunning) {
        CloseShare(
            header_ && header_->magic == openxr_share::kMagic && header_->version != openxr_share::kVersion
                ? "the layer's memory has another layout: install the layer from this build"
                : nullptr
        );

        return false;
    }

    layerEvent_.Reset(OpenEventW(SYNCHRONIZE, FALSE, openxr_share::kNewPictureEvent));
    header_->readerSigned.store(Counter());  // signed before the layer looks: it makes its textures now
    if (UniqueHandle reader(OpenEventW(EVENT_MODIFY_STATE, FALSE, openxr_share::kReaderEvent)); reader) {
        SetEvent(reader.Get());
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        adapter_.LowPart = header_->adapterLow;
        adapter_.HighPart = header_->adapterHigh;
    }

    session_ = 0;  // the session's state starts afresh (ReadFrames)
    CoreLog("OpenXR: the layer runs in the game (process %u)", header_->processId);
    return true;
}

void OpenXrSource::CloseShare(const char* why) {
    if (why) {
        CoreLog("OpenXR: %s", why);
    }
    header_.Reset();
    mapping_.Reset();
    layerEvent_.Reset();
    waitingCount_ = 0;
    clockValid_ = false;
}

void OpenXrSource::ReadClock() {
    int64_t counter = 0, time = 0;
    if (!openxr_share::ReadClock(*header_, &counter, &time)) {
        return;
    }
    clockOffset_ = double(counter) / frequency_ - double(time) * 1e-9;
    if (!clockValid_) {
        CoreLog("OpenXR: the runtime's clock read (its time minus the counter: %.6f s)", -clockOffset_);
    }

    clockValid_ = true;
}

void OpenXrSource::ReadFrames(double now) {
    openxr_share::Header& h = *header_;
    const uint64_t frames = h.frames.load();
    if (frames + 1 < nextFrame_) {
        nextFrame_ = 1;  // counted anew
    }
    if (frames >= nextFrame_ + openxr_share::kRecords) {
        nextFrame_ = frames - openxr_share::kRecords + 1;
    }
    for (; nextFrame_ <= frames; nextFrame_++) {
        openxr_share::Record r;
        if (!openxr_share::ReadRecord(h, nextFrame_, &r) || !clockValid_) {
            continue;
        }
        const double slot = SourceSeconds(r.displayTime);

        NoteHandover(r);
        if (trace_.IsOpen()) {
            trace_.Printf(
                "F,%llu,%.6f,%.6f,%d,%d,%u,%u,%u,%u\n",
                static_cast<unsigned long long>(r.frame),
                double(r.endFrameCounter) / frequency_,
                slot,
                r.texture,
                r.gameImage,
                r.imagesHeld,
                r.releasedDuringEnd,
                r.endThread,
                r.releaseThread
            );
        }

        // OpenXR's head (its view space) is halfway between the eyes; the left eye's turn from it is its
        // cant.
        const Quat left = QuatOf(r.eye[0]), right = QuatOf(r.eye[1]);
        const Quat head = Normalize(Slerp(left, right, 0.5f));

        EyeInfo eye;
        eye.leftTan = std::tan(-r.fov[0].left), eye.rightTan = std::tan(r.fov[0].right);
        eye.upTan = std::tan(r.fov[0].up), eye.downTan = std::tan(-r.fov[0].down);
        eye.hmdToEye = Normalize(Mul(Conj(head), left));
        eye.encodedTwice = false;  // the game's picture as it drew it

        {
            std::lock_guard<std::mutex> lock(mutex_);
            poses_.AddPose(slot, head);
            poses_.AddPrediction(slot, head);  // what the game drew the frame for this slot with
            eye_ = eye;
        }

        if (r.texture < 0 || r.texture >= openxr_share::kTextures) {
            continue;
        }
        if (waitingCount_ == kWaiting) {  // never finished: forget the oldest
            std::copy(waiting_ + 1, waiting_ + kWaiting, waiting_);
            waitingCount_--;
            lost_++;
        }

        waiting_[waitingCount_++] = {r.frame, r.texture, slot, r.width, r.height};
    }

    (void)now;
}

void OpenXrSource::LogProtection(uint32_t protection) const {
    if (protection == openxr_share::kProtected) {
        CoreLog("OpenXR: the game's device context is protected for use from several threads");
    } else if (protection == openxr_share::kUnprotected) {
        CoreLog(
            "OpenXR: the game's device context is not protected for use from several threads (the layer "
            "copies on it from the thread that ends the frame)"
        );
    }
}

// How the game hands its images over: logged once a session, when first seen (the trace has every frame).
void OpenXrSource::NoteHandover(const openxr_share::Record& r) {
    if (!loggedOtherThread_ && r.releaseThread && r.endThread && r.releaseThread != r.endThread) {
        loggedOtherThread_ = true;
        CoreLog(
            "OpenXR: the game releases its images on another thread (%u) than it ends its frames on (%u)",
            r.releaseThread,
            r.endThread
        );
    }

    if (!loggedReleaseDuringEnd_ && r.releasedDuringEnd) {
        loggedReleaseDuringEnd_ = true;
        CoreLog(
            "OpenXR: the game released %u image(s) while frame %llu was in xrEndFrame",
            r.releasedDuringEnd,
            static_cast<unsigned long long>(r.frame)
        );
    }
}

void OpenXrSource::FinishCopies() {
    openxr_share::Header& h = *header_;
    const double period = period_.load();
    int kept = 0;

    for (int i = 0; i < waitingCount_; i++) {
        const Waiting w = waiting_[i];
        const uint64_t held = h.textureFrame[w.texture].load();

        bool later = false;  // a later frame went into the same texture: this one's picture is gone
        for (int j = i + 1; j < waitingCount_; j++) {
            later = later || waiting_[j].texture == w.texture;
        }

        if (held != w.layerFrame) {
            if (held > w.layerFrame || later) {
                lost_++;
            } else {
                waiting_[kept++] = w;  // its copy isn't finished yet
            }

            continue;
        }

        // It arrives for the hosts now, when its copy is seen finished (a frame or two after the game
        // handed it over: the layer checks its copies at the next frames).
        const double arrival = Now();
        const ArrivalResult a = arrival_->Add({arrival, 0, w.slot, period});

        Finished f;
        f.frame.buffer = w.texture;
        f.frame.serial = ++serial_;
        f.frame.time = arrival;
        f.frame.smoothTime = a.smooth;
        f.frame.synced = true;
        f.frame.slotLead = a.slot - arrival;
        f.frame.slotTime = a.slot;   // the slot it is shown in, at the rhythm frames arrive in
        f.frame.drawnSlot = w.slot;  // the slot the game drew it for: its pose (stage 2)
        f.layerFrame = w.layerFrame;
        f.width = w.width, f.height = w.height;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            finished_[(finishedHead_ + finishedCount_) % kFinished] = f;
            if (finishedCount_ < kFinished) {
                finishedCount_++;
            } else {
                finishedHead_ = (finishedHead_ + 1) % kFinished;
            }
        }

        if (trace_.IsOpen()) {
            trace_.Printf(
                "A,%llu,%llu,%.6f\n",
                static_cast<unsigned long long>(w.layerFrame),
                static_cast<unsigned long long>(f.frame.serial),
                arrival
            );
        }

        framesArrived_++;
        stats_.Frame();
        stats_.Removed(arrival - a.smooth);
        lastFinished_ = arrival;
        if (idle_) {
            idle_ = false;
            CoreLog("OpenXR: game frames");
        }

        SetEvent(frameEvent_.Get());
    }

    waitingCount_ = kept;
}

// Wakes for the layer's finished copies, Stop, or twice a second (to sign as a reader, and to notice the
// game gone); with no layer, once a second to look for it.
void OpenXrSource::Run() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    lastFinished_ = Now();
    while (running_) {
        wakeups_++;
        if (!header_) {
            if (session_event_) {
                ResetEvent(session_event_.Get());  // before looking: a session starting now sets it again
            }

            if (!OpenShare()) {
                // Until the layer's session starts (it sets the event), or a look once a second.
                HANDLE wait[2] = {stop_.Get(), session_event_.Get()};
                WaitForMultipleObjects(session_event_ ? 2 : 1, wait, FALSE, 1000);
                continue;
            }
        }

        openxr_share::Header& h = *header_;
        const int64_t counter = Counter();
        h.readerSigned.store(counter);
        const double now = double(counter) / frequency_;
        if (h.state.load() != openxr_share::kRunning) {
            CloseShare("the game's session ended");
            continue;
        }

        if (now - double(h.layerSigned.load()) / frequency_ > openxr_share::kLayerTimeoutS) {
            CloseShare("the layer stopped signing (the game closed?)");
            continue;
        }

        if (h.session.load() != session_) {
            // A new session of the game: its frames count anew, and its timing is its own.
            session_ = h.session.load();
            nextFrame_ = 1;
            waitingCount_ = 0;
            arrival_->Reset();
            stats_.Reset();
            loggedOtherThread_ = loggedReleaseDuringEnd_ = false;
            LogProtection(h.contextProtection);
            std::lock_guard<std::mutex> lock(mutex_);
            poses_.Clear();
            finishedCount_ = 0;  // the last session's pictures are gone with it
        }

        ReadClock();
        if (const int64_t ns = h.displayPeriod.load()) {
            period_ = double(ns) * 1e-9;
        }
        ReadFrames(now);
        FinishCopies();
        const ArrivalStats::Report report = stats_.Tick(now);
        if (report.window) {
            fps_ = report.fps;
        }
        if (report.noise) {
            CoreLog("OpenXR: arrival timing noise removed: %.2f ms rms", report.noiseMs);
        }
        if (!idle_ && now - lastFinished_ > kIdleAfter) {
            idle_ = true;
            fps_ = 0;
            CoreLog("OpenXR: no game frames for %.0f s: idle", kIdleAfter);
        }

        HANDLE wait[2] = {stop_.Get(), layerEvent_.Get()};
        WaitForMultipleObjects(layerEvent_ ? 2 : 1, wait, FALSE, 500);
    }
}

// ---- The hosts' side. ----

GameFrame OpenXrSource::LatestFrame() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (finishedCount_ == 0) {
        return {};
    }
    return finished_[(finishedHead_ + finishedCount_ - 1) % kFinished].frame;
}

bool OpenXrSource::NextFrame(uint64_t after, GameFrame* f) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < finishedCount_; i++) {
        const Finished& e = finished_[(finishedHead_ + i) % kFinished];
        if (e.frame.serial > after) {
            *f = e.frame;
            return true;
        }
    }

    return false;
}

bool OpenXrSource::FindFinished(uint64_t serial, Finished* out) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < finishedCount_; i++) {
        const Finished& e = finished_[(finishedHead_ + i) % kFinished];
        if (e.frame.serial == serial) {
            *out = e;
            return true;
        }
    }

    return false;
}

Quat OpenXrSource::HeadAt(double t) {
    std::lock_guard<std::mutex> lock(mutex_);
    return poses_.At(t);
}

void OpenXrSource::HeadsAt(const double* t, int n, Quat* out) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < n; i++) {
        out[i] = poses_.At(t[i]);
    }
}

bool OpenXrSource::PredictedHeadAt(double slot, Quat* q) {
    std::lock_guard<std::mutex> lock(mutex_);
    return poses_.PredictionFor(slot, 0.25 * period_.load(), q);
}

void OpenXrSource::PredictedHeadsAt(const double* slots, int n, Quat* out) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < n; i++) {
        out[i] = poses_.At(slots[i]);
    }
    poses_.PredictionsFor(slots, n, 0.25 * period_.load(), out);
}

void OpenXrSource::PredictedHeadsBetween(const double* t, int n, Quat* out) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < n; i++) {
        out[i] = poses_.At(t[i]);
    }
    poses_.PredictionsBetween(t, n, 1.5 * period_.load(), out);
}

void OpenXrSource::OpenPictures(ID3D11Device* device) {
    pictures_.Open(device);
}

bool OpenXrSource::CopyPicture(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    const GameFrame& f,
    FrameCopy* into,
    bool shared
) {
    Finished e;
    if (!FindFinished(f.serial, &e)) {
        return false;
    }
    return pictures_.Copy(device, context, f.buffer, e.layerFrame, e.width, e.height, into, shared);
}

void OpenXrSource::PollPictures(ID3D11DeviceContext* context) {
    pictures_.Poll(context);
}

uint64_t OpenXrSource::ThreadCycles() {
    return ::ThreadCycles(thread_);
}

uint64_t OpenXrSource::TakeStats(double seconds, void (*line)(const char*)) {
    const uint64_t wakeups = wakeups_.exchange(0), lost = lost_.exchange(0), gone = pictures_.TakeGone();
    const uint64_t arrived = framesArrived_.load(), frames = arrived - statArrived_;
    statArrived_ = arrived;
    if (!line) {
        return wakeups;
    }
    char text[200];
    snprintf(
        text,
        sizeof(text),
        "OpenXR frames: %llu in %.0f s with their picture; pictures gone before they were copied: %llu "
        "(the "
        "layer's), %llu (a host's)",
        static_cast<unsigned long long>(frames),
        seconds,
        static_cast<unsigned long long>(lost),
        static_cast<unsigned long long>(gone)
    );

    line(text);
    return wakeups;
}
