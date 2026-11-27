// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// fake_openxr.exe: a fake game with the OpenXR layer in it, for end-to-end tests of
// simoptix_vr_mirror.exe with frame_source=openxr. It makes the OpenXR calls a game makes, into the
// layer's own frame code (fake_openxr_layer.h): a swapchain of three images, each frame's image acquired,
// drawn into and released, and the frame ended. The layer picks the image each frame shows and hands it
// to the tool as in the game: the shared memory, the frame records, the three shared textures, the copy
// after each frame and its "done" marker, and the clock reading. The tool runs unmodified and finds it as
// it finds the layer in the game.
//
//  - Display: 90 Hz. Each frame begins one frame before the vsync it ends at, is drawn for the slot after
//    that (two vsyncs ahead, as OpenXR's xrWaitFrame predicts) with the head pose predicted for it at its
//    beginning, and ends near its vsync with ~0.5 ms of timing noise (`arrival_jitter_ms`).
//  - Image: the left eye, 5084x4168 in R8G8B8A8_UNORM_SRGB, LMU's field of view on the Dream Air through
//    Pimax's OpenXR runtime (52.6 deg out, 47.6 in, 44.6 up and down); the direction codes or the cockpit
//    (fake_game_image.h), encoded once as a game draws it.
//  - Poses: both eyes, each turned 1 deg outward from the head and 32 mm to its side; the head halfway.
//  - Clock: XrTime is the performance counter in nanoseconds plus 123.456789 s, so the tool must use the
//    clock reading.
//  - The game misses slots (`drop_rate`) or falls behind (`queue_lag`, with `drop_rate`): its frames end up
//    to that many slots late for the slot they were drawn for, which they still report.
//  - No game yet (`game_start_s`): the layer's memory appears that long after the fake starts.
//  - A render thread (`render_thread=1`): one thread draws each frame and releases its image as soon as
//    the frame begins, while another ends the frames, and the runtime holds each xrEndFrame until the
//    frame's vsync. So the next frame's image is released while the frame before is still in xrEndFrame.
//    Both threads use the device's one context, protected for that as a game must. `drop_rate` and
//    `queue_lag` don't apply, and the frames aren't timed for the performance overlay.
//  - Without the render thread, the frames are timed for the performance overlay while it reads, as the
//    layer times the game's: each frame waits for its beginning in xrWaitFrame and begins right after.
//  - Stalls (`stall_every_s`, `stall_ms`, with the render thread): that often, a frame takes that much
//    longer to draw. It ends late for the slot it was drawn for, the frames due meanwhile never come,
//    and the next frame follows it at once.
//  - A recording, for auto sync's e2e test (`plan`): a cut of a real recording of LMU's menus (`menu_cut`:
//    its folder) played on the plan, each frame drawn from the camera it was recorded from
//    (fake_openxr_recording.h). The plan starts once the event named by `start_event` is set, or at once
//    without one. The menu cut's sound (`menu_sound`, mono 32-bit floats at `menu_sound_rate`, its first
//    sample `menu_sound_after` seconds after the plan's start) plays through Windows' default output device
//    `menu_sound_lag_ms` late (fake_openxr_sound.h). Once the event named by `restart_event` is set, the
//    game restarts: its frames and sound stop for `restart_s`, and the cut plays again from its start.
//  - The layer's own log (`layer_log=1`), for the local files test: started as the layer starts it in LMU,
//    in %LOCALAPPDATA%\simoptix, so it follows the Debug of the sources that read the frames and the frame
//    times. Without it, the layer's lines go to the console.
//
// Configuration: the file named by SIMOPTIX_FAKE_OPENXR_SCENARIO (key=value lines). Its log (key `log`) has
// the true head motion and every frame drawn, in fake_pvr's format, so the runner measures both fakes
// alike. It runs until the event named by key `stop_event` is set.

#include <windows.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <dxgi.h>
#include <timeapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <thread>

#include "common/rot.h"
#include "fake_game_image.h"
#include "fake_game_motion.h"
#include "fake_openxr_layer.h"
#include "fake_openxr_recording.h"
#include "fake_openxr_sound.h"
#include "shared/unique_handle.h"

using Microsoft::WRL::ComPtr;

namespace {

    double g_frequency = 1;

    int64_t Counter() {
        LARGE_INTEGER c;
        QueryPerformanceCounter(&c);
        return c.QuadPart;
    }

    double Now() {
        return double(Counter()) / g_frequency;
    }

    void WaitUntil(double t) {
        while (true) {
            const double left = t - Now();
            if (left <= 0) {
                return;
            }
            if (left > 0.002) {
                Sleep(DWORD((left - 0.0015) * 1000));
            } else {
                YieldProcessor();
            }
        }
    }

    constexpr int64_t kClockOffsetNs = 123456789000;  // XrTime minus the performance counter (ns)

    bool Clock(int64_t counter, int64_t* time) {
        *time = int64_t(std::llround(double(counter) * (1e9 / g_frequency))) + kClockOffsetNs;
        return true;
    }

    int64_t ToXrTime(double seconds) {
        return int64_t(std::llround(seconds * 1e9)) + kClockOffsetNs;
    }

    constexpr float kDegToRad = 0.017453293f;

    struct Config {
        std::string motion = "still", log, stopEvent, startEvent, restartEvent;
        double refreshHz = 90;
        double slotStepLeadMs = 1.0;
        double arrivalPhaseMs = 0.2;
        double arrivalJitterMs = 0.5;
        double dropRate = 0;
        int queueLag = 0;
        double gameStartS = 0;
        double drawnFov = 1;
        bool cockpit = false;
        bool renderThread = false;
        double stallEveryS = 0;  // 0: no stalls
        double stallMs = 64;
        int width = 5084, height = 4168;
        RecordingPlayback::Settings recording;  // no plan: no recordings
        std::string menuSound;                  // empty: silent
        double menuSoundAfter = 0, menuSoundLagMs = 0, menuSoundRate = 48000;
        double restartS = 3;
        bool layerLog = false;

        bool Load() {
            char path[MAX_PATH] = {};
            if (!GetEnvironmentVariableA("SIMOPTIX_FAKE_OPENXR_SCENARIO", path, MAX_PATH)) {
                return false;
            }
            FILE* f = nullptr;
            if (fopen_s(&f, path, "r") != 0 || !f) {
                return false;
            }
            char line[512];
            std::map<std::string, std::string> kv;
            while (fgets(line, sizeof(line), f)) {
                std::string s = line;
                while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
                    s.pop_back();
                }
                const size_t eq = s.find('=');
                if (eq != std::string::npos && s[0] != '#') {
                    kv[s.substr(0, eq)] = s.substr(eq + 1);
                }
            }

            fclose(f);
            auto num = [&](const char* k, double def) { return kv.count(k) ? atof(kv[k].c_str()) : def; };

            if (kv.count("motion")) {
                motion = kv["motion"];
            }
            if (kv.count("log")) {
                log = kv["log"];
            }
            if (kv.count("stop_event")) {
                stopEvent = kv["stop_event"];
            }
            if (kv.count("start_event")) {
                startEvent = kv["start_event"];
            }
            if (kv.count("restart_event")) {
                restartEvent = kv["restart_event"];
            }

            refreshHz = num("refresh_hz", refreshHz);
            slotStepLeadMs = num("slot_step_lead_ms", slotStepLeadMs);
            arrivalPhaseMs = num("arrival_phase_ms", arrivalPhaseMs);
            arrivalJitterMs = num("arrival_jitter_ms", arrivalJitterMs);
            dropRate = num("drop_rate", dropRate);
            queueLag = int(num("queue_lag", 0));
            gameStartS = num("game_start_s", 0);
            drawnFov = num("drawn_fov", 1);
            cockpit = num("cockpit", 0) != 0;
            renderThread = num("render_thread", 0) != 0;
            stallEveryS = num("stall_every_s", stallEveryS);
            stallMs = num("stall_ms", stallMs);
            width = int(num("width", width));
            height = int(num("height", height));
            recording.menuCut = kv.count("menu_cut") ? kv["menu_cut"] : "";
            recording.plan = kv.count("plan") ? kv["plan"] : "";
            menuSound = kv.count("menu_sound") ? kv["menu_sound"] : "";
            menuSoundAfter = num("menu_sound_after", 0);
            restartS = num("restart_s", restartS);
            menuSoundLagMs = num("menu_sound_lag_ms", 0);
            menuSoundRate = num("menu_sound_rate", menuSoundRate);
            layerLog = num("layer_log", 0) != 0;
            return true;
        }
    };

    // LMU's left eye through Pimax's OpenXR runtime (the layer's log), and the eyes' turn
    // outward from the head.
    const openxr_share::Fov kLeftFov = {
        -52.6f * kDegToRad,
        47.6f * kDegToRad,
        44.6f * kDegToRad,
        -44.6f * kDegToRad,
    };

    const openxr_share::Fov kRightFov = {
        -47.6f * kDegToRad,
        52.6f * kDegToRad,
        44.6f * kDegToRad,
        -44.6f * kDegToRad,
    };

    const Quat kEyeCant[2] = {{0, 0.0087265f, 0, 0.9999619f}, {0, -0.0087265f, 0, 0.9999619f}};  // 1 deg out
    constexpr float kHalfIpd = 0.032f;                                                           // metres

    class Game {
    public:
        bool Init() {
            if (!config_.Load()) {
                fprintf(stderr, "fake_openxr: no scenario (SIMOPTIX_FAKE_OPENXR_SCENARIO)\n");
                return false;
            }

            std::string error;
            if (!motion_.Parse(config_.motion, &error)) {
                fprintf(stderr, "fake_openxr: %s\n", error.c_str());
                return false;
            }

            if (!config_.stopEvent.empty()) {
                stop_.Reset(OpenEventA(SYNCHRONIZE, FALSE, config_.stopEvent.c_str()));
            }

            if (!config_.startEvent.empty()) {
                planStart_.Reset(OpenEventA(SYNCHRONIZE, FALSE, config_.startEvent.c_str()));
            }

            if (!config_.restartEvent.empty()) {
                restart_.Reset(OpenEventA(SYNCHRONIZE, FALSE, config_.restartEvent.c_str()));
            }

            if (config_.layerLog && !FakeOpenXrLayer::StartLog()) {
                fprintf(stderr, "fake_openxr: the layer's log didn't start\n");
                return false;
            }

            period_ = 1.0 / config_.refreshHz;
            start_ = Now();
            if (!config_.log.empty()) {
                fopen_s(&log_, config_.log.c_str(), "w");
            }
            if (log_) {
                fprintf(
                    log_,
                    "# fake_openxr log. motion=%s start=%.6f period=%.9f\n",
                    config_.motion.c_str(),
                    start_,
                    period_
                );

                fprintf(
                    log_,
                    "# pose,time,qx,qy,qz,qw  |  frame,index,begin,target_slot,arrival,qx,qy,qz,qw,lag\n"
                );

                const Quat& c = kEyeCant[0];
                const openxr_share::Fov& fov = kLeftFov;

                // the image is the left eye; its field of view as tangents, as the OpenXR source reads it
                fprintf(log_, "eyecant,%.7f,%.7f,%.7f,%.7f\n", c.x, c.y, c.z, c.w);
                fprintf(
                    log_,
                    "eyefov,%.7f,%.7f,%.7f,%.7f\n",
                    std::tan(-fov.left),
                    std::tan(fov.right),
                    std::tan(fov.up),
                    std::tan(-fov.down)
                );
            }

            return CreateDevice();
        }

        void Run() {
            std::thread poses([this] {
                try {
                    PoseLogLoop();
                } catch (...) {
                    fprintf(stderr, "fake OpenXR: the pose thread stopped by an unexpected error\n");
                }
            });

            timeBeginPeriod(1);

            // No game yet: the layer's memory appears only when the game's session starts.
            while (!Stopped() && Now() < start_ + config_.gameStartS) {
                Sleep(10);
            }

            const int64_t periodNs = int64_t(std::llround(period_ * 1e9));

            if (!Stopped() && layer_.Start(device_.Get(), config_.width, config_.height, periodNs, Clock)) {
                if (config_.renderThread) {
                    ThreadedLoop();
                } else {
                    GameLoop();
                }

                LogStats();  // the stretch since the last 10 s
                layer_.Stop();
            }

            timeEndPeriod(1);
            running_ = false;
            poses.join();
            if (log_) {
                fclose(log_);
            }
        }

    private:
        // A frame drawn and its image released, waiting to be ended (the render thread's).
        struct Drawn {
            long long index = 0;
            double begin = 0, target = 0, end = 0;  // performance-counter seconds
            Quat head;
            int lag = 0;  // slots late for the slot it was drawn for
        };

        bool Stopped() const { return stop_ && WaitForSingleObject(stop_.Get(), 0) == WAIT_OBJECT_0; }

        Quat TrueHead(double t) const { return motion_.At(std::max(0.0, t - start_)); }

        // The pose predicted at `from` for `target`: the head's turning rate over the last 4 ms, held on.
        Quat Predicted(double from, double target) const {
            const double dt = 0.004;
            const Quat q = TrueHead(from), prev = TrueHead(from - dt);
            Quat delta = Normalize(Mul(q, Conj(prev)));  // world-frame rotation over dt
            if (delta.w < 0) {
                delta = {-delta.x, -delta.y, -delta.z, -delta.w};
            }
            const float angle = static_cast<float>(RotationAngleAtan2(prev, q));
            const float s = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
            if (angle < 1e-7f || s < 1e-7f) {
                return q;
            }
            const float ahead = float(angle * (target - from) / dt);
            const Quat step{
                delta.x / s * std::sin(ahead / 2),
                delta.y / s * std::sin(ahead / 2),
                delta.z / s * std::sin(ahead / 2),
                std::cos(ahead / 2)
            };

            return Normalize(Mul(step, q));
        }

        bool CreateDevice() {
            ComPtr<IDXGIFactory1> factory;
            ComPtr<IDXGIAdapter1> adapter;
            if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))
                || FAILED(factory->EnumAdapters1(0, &adapter))) {
                return false;
            }

            if (FAILED(D3D11CreateDevice(
                    adapter.Get(),
                    D3D_DRIVER_TYPE_UNKNOWN,
                    nullptr,
                    0,
                    nullptr,
                    0,
                    D3D11_SDK_VERSION,
                    &device_,
                    nullptr,
                    &context_
                ))) {
                return false;
            }

            // Two threads use the one context: a game that does must protect it.
            if (config_.renderThread) {
                ComPtr<ID3D11Multithread> multithread;

                if (FAILED(context_.As(&multithread))) {
                    return false;
                }

                multithread->SetMultithreadProtected(TRUE);
            }

            if (!config_.recording.plan.empty()) {
                if (!playback_.Init(config_.recording, device_.Get(), config_.width, config_.height)) {
                    return false;
                }

                playing_ = true;
            }

            if (!config_.menuSound.empty()) {
                if (!sound_.Load(config_.menuSound, config_.menuSoundRate)) {
                    fprintf(stderr, "fake_openxr: no sound in %s\n", config_.menuSound.c_str());
                    return false;
                }

                sound_.Start(config_.menuSoundLagMs / 1000);
            }

            return image_.Init(device_.Get(), false);
        }

        // A recording's plan and its sound start once start_event is set (at once without it). Once
        // restart_event is set, they stop, and after restart_s play from the start again, the sound as far
        // after the frames as at first.
        void FollowEvents() {
            auto set = [](const UniqueHandle& event) {
                return event && WaitForSingleObject(event.Get(), 0) == WAIT_OBJECT_0;
            };

            if (!playing_) {
                return;
            }

            const double now = Now();

            if (!begun_ && (!planStart_ || set(planStart_))) {
                playback_.Begin(now);
                sound_.Begin(now + config_.menuSoundAfter);
                begun_ = true;
                fprintf(stderr, "fake_openxr: the recording plays\n");
            } else if (begun_ && !restarted_ && set(restart_)) {
                playback_.Restart(now, config_.restartS);
                sound_.Restart(now, now + config_.restartS + config_.menuSoundAfter);
                restarted_ = true;
                fprintf(stderr, "fake_openxr: the game restarts\n");
            }
        }

        // The head pose of the frame drawn for `target`, begun at `begin`: a recording's camera while one
        // plays, else the pose predicted then.
        Quat HeadFor(double begin, double target) {
            return playing_ ? playback_.Head(target, kEyeCant[0]) : Predicted(begin, target);
        }

        void Draw(ID3D11RenderTargetView* target, const Quat& head, double sceneTime) {
            FakeGameImage::Look look;
            look.leftTan = std::tan(-kLeftFov.left), look.rightTan = std::tan(kLeftFov.right);
            look.upTan = std::tan(kLeftFov.up), look.downTan = std::tan(-kLeftFov.down);

            if (playing_) {
                playback_.Draw(context_.Get(), target, look.leftTan, look.rightTan, look.upTan, look.downTan);
                return;
            }

            look.drawnFov = float(config_.drawnFov);
            look.cockpit = config_.cockpit;
            look.encodedTwice = false;  // as the game draws it: the layer copies it so
            image_.Draw(
                context_.Get(),
                target,
                config_.width,
                config_.height,
                Normalize(Mul(head, kEyeCant[0])),
                look,
                nullptr,
                sceneTime
            );
        }

        static openxr_share::Pose EyePose(const Quat& head, int eye) {
            const Quat q = Normalize(Mul(head, kEyeCant[eye]));
            const Vec3 p = Rotate(head, {eye == 0 ? -kHalfIpd : kHalfIpd, 0, 0});
            return {q.x, q.y, q.z, q.w, p.x, p.y, p.z};
        }

        FakeOpenXrLayer::FrameEnd FrameEndOf(const Quat& head, double target) const {
            FakeOpenXrLayer::FrameEnd f;
            f.displayTime = ToXrTime(target);
            f.eye[0] = EyePose(head, 0), f.eye[1] = EyePose(head, 1);
            f.fov[0] = kLeftFov, f.fov[1] = kRightFov;
            return f;
        }

        void LogFrame(const Drawn& d, double ended) {
            std::lock_guard<std::mutex> lock(logMutex_);

            if (log_) {
                fprintf(
                    log_,
                    "frame,%lld,%.6f,%.6f,%.6f,%.7f,%.7f,%.7f,%.7f,%d\n",
                    d.index,
                    d.begin,
                    d.target,
                    ended,
                    d.head.x,
                    d.head.y,
                    d.head.z,
                    d.head.w,
                    d.lag
                );
            }
        }

        void LogStats() {
            const OpenXrPublisherStats s = layer_.TakeStats();
            const OpenXrFrameTimingStats t = layer_.TakeTimingStats();
            std::lock_guard<std::mutex> lock(logMutex_);

            if (log_) {
                fprintf(
                    log_,
                    "# frame source: %u frames, %u copied, not copied: %u (reader), %u (copy pending)\n",
                    s.frames,
                    s.copied,
                    s.readerBusy,
                    s.copyPending
                );
                fprintf(log_, "# frame timing: %u frames timed\n", t.frames);
            }
        }

        // One frame per display slot (fake_pvr's timing): begins one frame before its vsync, drawn with the
        // pose predicted then for the slot after its vsync, ends near its vsync; behind (queue_lag), drawn
        // for an earlier slot than that and ending ~4 ms later.
        void GameLoop() {
            std::mt19937 rng(12345);
            std::normal_distribution<double> jitter(0, config_.arrivalJitterMs / 1000);
            std::uniform_real_distribution<double> uniform(0, 1);

            const double lead = config_.slotStepLeadMs / 1000;
            long long k = static_cast<long long>(std::ceil((Now() - start_) / period_)) + 1;
            int lag = 0;
            double lastStats = Now();

            while (!Stopped()) {
                const double vsync = start_ + k * period_;
                const double begin = vsync - period_ - lead;
                const double target = vsync + period_ - lag * period_;
                double end = vsync + config_.arrivalPhaseMs / 1000 + jitter(rng) + (lag > 0 ? 0.004 : 0);
                end = std::clamp(end, vsync - 0.95 * lead, vsync + 0.4 * period_);

                FollowEvents();
                // A frame drawn is held in xrWaitFrame until it is drawn, as a runtime holds a game, so its
                // CPU time is the drawing's. A restart of a recording: the game's frames stop for a while.
                const bool drop = uniform(rng) < config_.dropRate || (playing_ && playback_.Paused(target));
                layer_.WaitFrame([begin, end, drop] {
                    WaitUntil(begin);

                    if (!drop) {
                        WaitUntil(end - 0.0015);
                    }
                });

                const Quat head = HeadFor(begin, target);
                if (!drop) {
                    layer_.BeginFrame();
                    Draw(layer_.Acquire(), head, target - start_);
                    layer_.Release();
                    layer_.EndFrame(FrameEndOf(head, target), [end] { WaitUntil(end); });
                    context_->Flush();  // as a game's frame goes to the GPU
                    LogFrame({k, begin, target, end, head, lag}, Now());
                }

                if (config_.queueLag > 0) {
                    if (drop && uniform(rng) < 0.6) {
                        lag = std::min(config_.queueLag, lag + 1);
                    } else if (!drop && lag > 0 && uniform(rng) < 0.05) {
                        lag--;
                    }
                }

                if (Now() - lastStats >= 10) {
                    lastStats = Now();
                    LogStats();
                }

                k++;
            }
        }

        // render_thread: this thread ends the frames the render thread drew, each held in the runtime until
        // its vsync, while the render thread goes on with the next.
        void ThreadedLoop() {
            std::thread render([this] {
                try {
                    RenderLoop();
                } catch (...) {
                    fprintf(stderr, "fake OpenXR: the render thread stopped by an unexpected error\n");
                }

                std::lock_guard<std::mutex> lock(drawnMutex_);
                renderDone_ = true;
                drawnChanged_.notify_all();
            });

            double lastStats = Now();
            Drawn d;

            while (TakeDrawn(&d)) {
                layer_.EndFrame(FrameEndOf(d.head, d.target), [&d] { WaitUntil(d.end); });
                context_->Flush();  // the layer's copy goes to the GPU, as in GameLoop
                LogFrame(d, Now());

                if (Now() - lastStats >= 10) {
                    lastStats = Now();
                    LogStats();
                }
            }

            render.join();
        }

        // The render thread: draws each frame when it begins and releases its image, at most one frame
        // ahead of the frames ended.
        void RenderLoop() {
            std::mt19937 rng(12345);
            std::normal_distribution<double> jitter(0, config_.arrivalJitterMs / 1000);

            const double lead = config_.slotStepLeadMs / 1000;
            long long k = static_cast<long long>(std::ceil((Now() - start_) / period_)) + 1;
            double nextStall = config_.stallEveryS > 0 ? start_ + config_.stallEveryS : 0;

            while (!Stopped()) {
                const double vsync = start_ + k * period_;
                const double begin = vsync - period_ - lead;
                const double target = vsync + period_;
                double end = vsync + config_.arrivalPhaseMs / 1000 + jitter(rng);
                end = std::clamp(end, vsync - 0.95 * lead, vsync + 0.4 * period_);

                WaitUntil(begin);
                const Quat head = HeadFor(begin, target);
                const bool stall = nextStall > 0 && Now() >= nextStall;
                ID3D11RenderTargetView* image = layer_.Acquire();

                if (stall) {
                    Sleep(DWORD(config_.stallMs));
                    nextStall += config_.stallEveryS;
                }

                Draw(image, head, target - start_);
                context_->Flush();  // as a game's frame goes to the GPU
                layer_.Release();

                Drawn d{k, begin, target, std::max(end, Now()), head, 0};
                d.lag = static_cast<int>(std::floor((d.end - vsync) / period_ + 0.5));

                if (!PutDrawn(d)) {
                    return;
                }

                // After a stall the frames due meanwhile never come: the next is the one beginning now.
                k = stall ? static_cast<long long>(std::floor((Now() - start_ + lead) / period_)) + 1 : k + 1;
            }
        }

        // Hands a drawn frame to the thread ending them once fewer than two wait; false once stopped.
        bool PutDrawn(const Drawn& d) {
            std::unique_lock<std::mutex> lock(drawnMutex_);

            while (drawn_.size() >= 2) {
                if (Stopped()) {
                    return false;
                }

                drawnChanged_.wait_for(lock, std::chrono::milliseconds(5));
            }

            drawn_.push_back(d);
            drawnChanged_.notify_all();
            return true;
        }

        // The oldest drawn frame not yet ended; false once the render thread has stopped and none wait.
        bool TakeDrawn(Drawn* d) {
            std::unique_lock<std::mutex> lock(drawnMutex_);

            while (drawn_.empty()) {
                if (renderDone_) {
                    return false;
                }

                drawnChanged_.wait_for(lock, std::chrono::milliseconds(5));
            }

            *d = drawn_.front();
            drawn_.pop_front();
            drawnChanged_.notify_all();
            return true;
        }

        // The true head at 1 kHz, for the runner's comparisons.
        void PoseLogLoop() {
            while (running_) {
                const double t = Now();
                const Quat q = TrueHead(t);
                {
                    std::lock_guard<std::mutex> lock(logMutex_);
                    if (log_) {
                        fprintf(log_, "pose,%.6f,%.7f,%.7f,%.7f,%.7f\n", t, q.x, q.y, q.z, q.w);
                    }
                }

                Sleep(1);
            }
        }

        Config config_;
        motion::Motion motion_;
        RecordingPlayback playback_;  // the recordings, when they play
        bool playing_ = false;
        RecordedSound sound_;  // plays while the game runs
        double period_ = 1.0 / 90, start_ = 0;
        UniqueHandle stop_, planStart_, restart_;
        bool begun_ = false, restarted_ = false;
        FILE* log_ = nullptr;
        std::mutex logMutex_;
        std::atomic<bool> running_{true};
        ComPtr<ID3D11Device> device_;
        ComPtr<ID3D11DeviceContext> context_;
        FakeGameImage image_;
        FakeOpenXrLayer layer_;

        // Frames between the render thread and the thread ending them (render_thread).
        std::mutex drawnMutex_;
        std::condition_variable drawnChanged_;
        std::deque<Drawn> drawn_;
        bool renderDone_ = false;
    };

}

int main() {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_frequency = double(f.QuadPart);
    Game game;
    if (!game.Init()) {
        return 1;
    }
    game.Run();
    return 0;
}
