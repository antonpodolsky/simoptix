// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// fake_openxr.exe: a fake game with the OpenXR layer in it, for end-to-end tests of
// simoptix_vr_mirror.exe with frame_source=openxr. It hands its frames to the tool exactly as the layer
// does, through the layer's own publisher (vr_mirror\frame_sources\openxr\openxr_publisher.*): the shared
// memory, the frame records, the three shared textures, the copy after each frame and its "done" marker,
// and the clock reading. The tool runs unmodified and finds it as it finds the layer in the game.
//
//  - Display: 90 Hz. Each frame begins one frame before the vsync it ends at, is drawn for the slot after
//    that (two vsyncs ahead, as OpenXR's xrWaitFrame predicts) with the head pose predicted for it at its
//    beginning, and ends near its vsync with ~0.5 ms of timing noise (`arrival_jitter_ms`).
//  - Picture: the left eye, 5084x4168 in R8G8B8A8_UNORM_SRGB, LMU's field of view on the Dream Air through
//    Pimax's OpenXR runtime (52.6 deg out, 47.6 in, 44.6 up and down); the direction codes or the cockpit
//    (fake_game_picture.h), encoded once as a game draws it.
//  - Poses: both eyes, each turned 1 deg outward from the head and 32 mm to its side; the head halfway.
//  - Clock: XrTime is the performance counter in nanoseconds plus 123.456789 s, so the tool must use the
//    clock reading to place the frames.
//  - The game misses slots (`drop_rate`) or falls behind (`queue_lag`, with `drop_rate`): its frames end up
//    to that many slots late for the slot they were drawn for, which they still report.
//  - No game yet (`game_start_s`): the layer's memory appears that long after the fake starts.
//
// Configuration: the file named by SIMOPTIX_FAKE_OPENXR_SCENARIO (key=value lines). Its log (key `log`) has
// the true head motion and every frame drawn, in fake_pvr's format, so the runner measures both fakes
// alike. It runs until the event named by key `stop_event` is set.

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <timeapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <thread>

#include "common/rot.h"
#include "fake_game_motion.h"
#include "fake_game_picture.h"
#include "shared/unique_handle.h"
#include "vr_mirror/frame_sources/openxr/openxr_publisher.h"

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

    bool Clock(void*, int64_t counter, int64_t* time) {
        *time = int64_t(std::llround(double(counter) * (1e9 / g_frequency))) + kClockOffsetNs;
        return true;
    }

    int64_t ToXrTime(double seconds) {
        return int64_t(std::llround(seconds * 1e9)) + kClockOffsetNs;
    }

    constexpr float kDegToRad = 0.017453293f;

    struct Config {
        std::string motion = "still", log, stopEvent;
        double refreshHz = 90;
        double slotStepLeadMs = 1.0;
        double arrivalPhaseMs = 0.2;
        double arrivalJitterMs = 0.5;
        double dropRate = 0;
        int queueLag = 0;
        double gameStartS = 0;
        double drawnFov = 1;
        bool cockpit = false;
        int width = 5084, height = 4168;

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

            refreshHz = num("refresh_hz", refreshHz);
            slotStepLeadMs = num("slot_step_lead_ms", slotStepLeadMs);
            arrivalPhaseMs = num("arrival_phase_ms", arrivalPhaseMs);
            arrivalJitterMs = num("arrival_jitter_ms", arrivalJitterMs);
            dropRate = num("drop_rate", dropRate);
            queueLag = int(num("queue_lag", 0));
            gameStartS = num("game_start_s", 0);
            drawnFov = num("drawn_fov", 1);
            cockpit = num("cockpit", 0) != 0;
            width = int(num("width", width));
            height = int(num("height", height));
            return true;
        }
    };

    // LMU's left eye through Pimax's OpenXR runtime (the layer's log, 2026-11-06), and the eyes' turn
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

                // the picture is the left eye
                fprintf(log_, "eyecant,%.7f,%.7f,%.7f,%.7f\n", c.x, c.y, c.z, c.w);
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
            if (!Stopped() && publisher_.Start(device_.Get(), Clock, nullptr)) {
                publisher_.SetDisplayPeriod(int64_t(std::llround(period_ * 1e9)));
                GameLoop();
                publisher_.Stop();
            }

            timeEndPeriod(1);
            running_ = false;
            poses.join();
            if (log_) {
                fclose(log_);
            }
        }

    private:
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
            const float angle = 2 * std::acos(std::min(1.0f, delta.w));
            const float s = std::sqrt(std::max(0.0f, 1 - delta.w * delta.w));
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

            // The swapchain image the game draws its left eye into (LMU's format and size).
            D3D11_TEXTURE2D_DESC td = {};
            td.Width = config_.width;
            td.Height = config_.height;
            td.MipLevels = td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            td.SampleDesc.Count = 1;
            td.BindFlags = D3D11_BIND_RENDER_TARGET;
            if (FAILED(device_->CreateTexture2D(&td, nullptr, &image_))) {
                return false;
            }
            device_->CreateRenderTargetView(image_.Get(), nullptr, &target_);
            return target_ && picture_.Init(device_.Get(), false);
        }

        void Draw(const Quat& head, double sceneTime) {
            FakeGamePicture::Look look;
            look.leftTan = std::tan(-kLeftFov.left), look.rightTan = std::tan(kLeftFov.right);
            look.upTan = std::tan(kLeftFov.up), look.downTan = std::tan(-kLeftFov.down);
            look.drawnFov = float(config_.drawnFov);
            look.cockpit = config_.cockpit;
            look.encodedTwice = false;  // as the game draws it: the layer copies it so
            picture_.Draw(
                context_.Get(),
                target_.Get(),
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

                WaitUntil(begin);
                const Quat head = Predicted(begin, target);
                const bool drop = uniform(rng) < config_.dropRate;
                if (!drop) {
                    WaitUntil(end - 0.0015);
                    Draw(head, target - start_);
                    WaitUntil(end);

                    // xrEndFrame went on to the runtime: the layer's part.
                    OpenXrPublishedFrame f;
                    f.displayTime = ToXrTime(target);
                    f.endFrameCounter = Counter();
                    f.eye[0] = EyePose(head, 0), f.eye[1] = EyePose(head, 1);
                    f.fov[0] = kLeftFov, f.fov[1] = kRightFov;
                    const D3D11_BOX box = {0, 0, 0, UINT(config_.width), UINT(config_.height), 1};
                    publisher_.Publish(f, image_.Get(), 0, box, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
                    context_->Flush();  // as a game's frame goes to the GPU

                    const double t = Now();
                    std::lock_guard<std::mutex> lock(logMutex_);
                    if (log_) {
                        fprintf(
                            log_,
                            "frame,%lld,%.6f,%.6f,%.6f,%.7f,%.7f,%.7f,%.7f,%d\n",
                            k,
                            begin,
                            target,
                            t,
                            head.x,
                            head.y,
                            head.z,
                            head.w,
                            lag
                        );
                    }
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
                    const OpenXrPublisherStats s = publisher_.TakeStats();
                    std::lock_guard<std::mutex> lock(logMutex_);
                    if (log_) {
                        fprintf(
                            log_,
                            "# frame source: %u frames, %u copied, not copied: %u (reader), %u (copy "
                            "pending)\n",
                            s.frames,
                            s.copied,
                            s.readerBusy,
                            s.copyPending
                        );
                    }
                }

                k++;
            }
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
        double period_ = 1.0 / 90, start_ = 0;
        UniqueHandle stop_;
        FILE* log_ = nullptr;
        std::mutex logMutex_;
        std::atomic<bool> running_{true};
        ComPtr<ID3D11Device> device_;
        ComPtr<ID3D11DeviceContext> context_;
        ComPtr<ID3D11Texture2D> image_;
        ComPtr<ID3D11RenderTargetView> target_;
        FakeGamePicture picture_;
        OpenXrPublisher publisher_;
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
