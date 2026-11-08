// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "fake_pvr_headset.h"

#include <dxgi1_2.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <random>

#include "fake_pvr_thumbs.h"

using Microsoft::WRL::ComPtr;

namespace fake_pvr {

    namespace {

        void WaitUntil(double t) {
            while (t - Now() > 0.002) {
                Sleep(1);
            }

            while (Now() < t) {
                YieldProcessor();
            }
        }

    }

    double Now() {
        static LARGE_INTEGER frequency = [] {
            LARGE_INTEGER f;
            QueryPerformanceFrequency(&f);
            return f;
        }();

        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        return double(counter.QuadPart) / double(frequency.QuadPart);
    }

    void Config::Load() {
        char path[MAX_PATH] = {};

        if (!GetEnvironmentVariableA("SIMOPTIX_FAKE_PVR_SCENARIO", path, MAX_PATH)) {
            return;
        }

        FILE* file = nullptr;

        if (fopen_s(&file, path, "r") != 0 || !file) {
            return;
        }

        std::map<std::string, std::string> values;
        char line[512];

        while (fgets(line, sizeof(line), file)) {
            std::string text = line;

            while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
                text.pop_back();
            }

            const size_t eq = text.find('=');

            if (eq != std::string::npos && text[0] != '#') {
                values[text.substr(0, eq)] = text.substr(eq + 1);
            }
        }

        fclose(file);

        auto number = [&](const char* key, double fallback) {
            return values.count(key) ? atof(values[key].c_str()) : fallback;
        };

        if (values.count("motion")) {
            motion = values["motion"];
        }

        if (values.count("log")) {
            log = values["log"];
        }

        if (values.count("replay")) {
            replay = values["replay"];
        }

        if (values.count("thumbs")) {
            thumbs = values["thumbs"];
        }

        refreshHz = number("refresh_hz", refreshHz);
        slotStepLeadMs = number("slot_step_lead_ms", slotStepLeadMs);
        arrivalPhaseMs = number("arrival_phase_ms", arrivalPhaseMs);
        arrivalJitterMs = number("arrival_jitter_ms", arrivalJitterMs);
        dropRate = number("drop_rate", dropRate);
        drawnFov = number("drawn_fov", drawnFov);
        gameStartS = number("game_start_s", gameStartS);
        cockpit = number("cockpit", 0) != 0;
        queueLag = int(number("queue_lag", 0));
    }

    bool Headset::Init() {
        config_.Load();

        std::string error;

        if (!motion_.Parse(config_.motion, &error)) {
            fprintf(stderr, "fake_pvr: %s\n", error.c_str());
            return false;
        }

        if (!config_.replay.empty() && !replay_.Load(config_.replay, &error)) {
            fprintf(stderr, "fake_pvr: %s\n", error.c_str());
            return false;
        }

        replaying_ = !config_.replay.empty();

        if (replaying_ && !config_.thumbs.empty()) {
            thumbTiles_ = thumbs::Load(config_.thumbs, &thumbPixels_, &error);

            if (thumbTiles_ != int(replay_.Frames().size())) {
                fprintf(
                    stderr,
                    "fake_pvr: %s\n",
                    thumbTiles_ ? "thumbnails don't match the replay's frames" : error.c_str()
                );

                return false;
            }
        }

        period_ = 1.0 / config_.refreshHz;
        start_ = Now();

        if (!config_.log.empty()) {
            fopen_s(&log_, config_.log.c_str(), "w");
        }

        if (log_) {
            fprintf(
                log_,
                "# fake_pvr log. motion=%s start=%.6f period=%.9f\n",
                config_.motion.c_str(),
                start_,
                period_
            );

            fprintf(
                log_,
                "# pose,time,qx,qy,qz,qw  |  frame,index,begin,target_slot,arrival,qx,qy,qz,qw,lag\n"
            );

            fprintf(
                log_,
                "# idle_calls,count,seconds: calls into the runtime while no game ran (game_start_s)\n"
            );

            const Quat& cant = kEyeCant[0];  // the image is the left eye
            fprintf(log_, "eyecant,%.7f,%.7f,%.7f,%.7f\n", cant.x, cant.y, cant.z, cant.w);
        }

        poseThread_ = std::thread([this] {
            try {
                PoseLogLoop();
            } catch (...) {
                fprintf(stderr, "fake PVR: the pose thread stopped by an unexpected error\n");
            }
        });

        return CreateDevice();
    }

    void Headset::Shutdown() {
        StopGame();

        running_ = false;

        if (poseThread_.joinable()) {
            poseThread_.join();
        }

        std::lock_guard<std::mutex> lock(logMutex_);

        if (log_) {
            fclose(log_);
        }

        log_ = nullptr;
    }

    Quat Headset::TrueHead(double t) const {
        return replaying_ ? replay_.HeadAt(t - start_) : motion_.At(std::max(0.0, t - start_));
    }

    Quat Headset::Tracked(double absTime) const {
        const double now = Now();

        if (absTime <= now + 0.0005) {
            return TrueHead(std::min(absTime, now));
        }

        Quat recorded;

        if (replaying_ && replay_.PredictionFor(absTime - start_, now - start_, period_, &recorded)) {
            return recorded;
        }

        double from = now;  // the moment the extrapolation starts at
        const double k = (absTime - start_) / period_;

        if (std::fabs(k - std::round(k)) < 1e-3) {
            // the slot became next
            const double asked = absTime - 2 * period_ - config_.slotStepLeadMs / 1000;

            if (asked <= now) {
                from = asked;
            }
        }

        const double dt = 0.004;
        const Quat q = TrueHead(from), previous = TrueHead(from - dt);
        Quat delta = Normalize(Mul(q, Conj(previous)));  // world-frame rotation over dt

        if (delta.w < 0) {
            delta = {-delta.x, -delta.y, -delta.z, -delta.w};
        }

        const float angle = 2 * std::acos(std::min(1.0f, delta.w));
        const float sinHalf = std::sqrt(std::max(0.0f, 1 - delta.w * delta.w));

        if (angle < 1e-7f || sinHalf < 1e-7f) {
            return q;
        }

        const float ahead = float(angle * (absTime - from) / dt);

        const Quat step{
            delta.x / sinHalf * std::sin(ahead / 2),
            delta.y / sinHalf * std::sin(ahead / 2),
            delta.z / sinHalf * std::sin(ahead / 2),
            std::cos(ahead / 2)
        };

        return Normalize(Mul(step, q));
    }

    double Headset::PredictedDisplayTime() const {
        if (replaying_) {
            return start_ + replay_.SlotAt(Now() - start_, period_);
        }

        const double lead = config_.slotStepLeadMs / 1000;
        const double k = std::floor((Now() + lead - start_) / period_);
        return start_ + (k + 2) * period_;
    }

    void Headset::StartGame() {
        if (gameRunning_) {
            return;
        }

        gameRunning_ = true;
        mirrorOnAt_ = Now();

        gameThread_ = std::thread([this] {
            try {
                if (WaitForGame()) {
                    GameLoop();
                }
            } catch (...) {
                fprintf(stderr, "fake PVR: the game thread stopped by an unexpected error\n");
            }
        });
    }

    void Headset::StopGame() {
        gameRunning_ = false;

        if (gameThread_.joinable()) {
            gameThread_.join();
        }
    }

    bool Headset::WaitForGame() {
        if (config_.gameStartS <= 0) {
            return true;
        }

        auto waitUntil = [this](double t) {
            while (gameRunning_ && Now() < t) {
                Sleep(10);
            }

            return gameRunning_.load();
        };

        const double from = mirrorOnAt_ + std::min(2.5, config_.gameStartS);

        if (!waitUntil(from)) {
            return false;
        }

        const long long before = calls_;

        if (!waitUntil(mirrorOnAt_ + config_.gameStartS)) {
            return false;
        }

        const long long counted = calls_ - before;
        std::lock_guard<std::mutex> lock(logMutex_);

        if (log_) {
            fprintf(log_, "idle_calls,%lld,%.6f\n", counted, Now() - from);
        }

        return true;
    }

    bool Headset::CreateDevice() {
        ComPtr<IDXGIFactory1> factory;

        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            return false;
        }

        ComPtr<IDXGIAdapter1> adapter;

        if (FAILED(factory->EnumAdapters1(0, &adapter))) {
            return false;
        }

        DXGI_ADAPTER_DESC1 adapterDesc;
        adapter->GetDesc1(&adapterDesc);
        luid_ = adapterDesc.AdapterLuid;

        const HRESULT hr = D3D11CreateDevice(
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
        );

        if (FAILED(hr)) {
            return false;
        }

        D3D11_TEXTURE2D_DESC textureDesc = {};
        textureDesc.Width = config_.width;
        textureDesc.Height = config_.height;
        textureDesc.MipLevels = textureDesc.ArraySize = 1;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;  // same as LMU's client mirror (format 29)
        textureDesc.SampleDesc.Count = 1;
        textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        textureDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;  // legacy shared handle, like pi_server's

        if (FAILED(device_->CreateTexture2D(&textureDesc, nullptr, &texture_))) {
            return false;
        }

        device_->CreateRenderTargetView(texture_.Get(), nullptr, &rtv_);

        ComPtr<IDXGIResource> resource;
        texture_.As(&resource);
        resource->GetSharedHandle(&sharedHandle_);

        event_.Reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));  // manual reset, like the real one

        if (!picture_.Init(device_.Get(), thumbTiles_ > 0)) {
            return false;
        }

        const D3D11_QUERY_DESC queryDesc = {D3D11_QUERY_EVENT, 0};

        if (FAILED(device_->CreateQuery(&queryDesc, &done_))) {
            return false;
        }

        return true;
    }

    void Headset::Render(const Quat& head, int tile, double sceneTime) {
        const bool real = tile >= 0 && tile < thumbTiles_;
        const size_t tileBytes = size_t(thumbs::kWidth) * thumbs::kHeight * 4;

        FakeGamePicture::Look look;
        look.leftTan = kFov[0].leftTan, look.rightTan = kFov[0].rightTan;
        look.upTan = kFov[0].upTan, look.downTan = kFov[0].downTan;
        look.drawnFov = float(config_.drawnFov);
        look.cockpit = config_.cockpit;
        look.encodedTwice = true;  // the client mirror's

        picture_.Draw(
            context_.Get(),
            rtv_.Get(),
            config_.width,
            config_.height,
            Normalize(Mul(head, kEyeCant[0])),
            look,
            real ? &thumbPixels_[tile * tileBytes] : nullptr,
            sceneTime
        );

        context_->End(done_.Get());
        context_->Flush();

        while (context_->GetData(done_.Get(), nullptr, 0, 0) == S_FALSE) {
            YieldProcessor();
        }
    }

    void Headset::GameLoop() {
        if (replaying_) {
            return ReplayLoop();
        }

        timeBeginPeriod(1);

        std::mt19937 rng(12345);
        std::normal_distribution<double> jitter(0, config_.arrivalJitterMs / 1000);
        std::uniform_real_distribution<double> uniform(0, 1);

        const double lead = config_.slotStepLeadMs / 1000;
        long long k = static_cast<long long>(std::ceil((Now() - start_) / period_)) + 1;
        int lag = 0;  // queue_lag: slots the game is behind

        while (gameRunning_) {
            const double vsync = start_ + k * period_;
            const double begin = vsync - period_ - lead;  // slot step one frame before arrival

            // The slot predicted at `begin`, or while the game is behind an earlier one, whose prediction
            // the runtime made then (Tracked).
            const double target = vsync + period_ - lag * period_;
            double arrival = vsync + config_.arrivalPhaseMs / 1000 + jitter(rng) + (lag > 0 ? 0.004 : 0);
            arrival = std::clamp(arrival, vsync - 0.95 * lead, vsync + 0.4 * period_);

            WaitUntil(begin);
            const Quat pose = Tracked(target);
            WaitUntil(arrival - 0.0015);  // the draw itself takes well under 1.5 ms

            const bool drop = uniform(rng) < config_.dropRate;

            if (!drop) {
                ResetEvent(event_.Get());
                Render(pose, -1, target - start_);
                WaitUntil(arrival);
                SetEvent(event_.Get());

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
                        pose.x,
                        pose.y,
                        pose.z,
                        pose.w,
                        lag
                    );
                }
            }

            // Behind (queue_lag): a dropped slot often leaves the game a slot further behind, and it
            // catches up a slot now and then (its next frame then for two slots on).
            if (config_.queueLag > 0) {
                if (drop && uniform(rng) < 0.6) {
                    lag = std::min(config_.queueLag, lag + 1);
                } else if (!drop && lag > 0 && uniform(rng) < 0.05) {
                    lag--;
                }
            }

            k++;
        }

        timeEndPeriod(1);
    }

    void Headset::ReplayLoop() {
        timeBeginPeriod(1);

        int index = -1;

        for (const replay::Frame& frame : replay_.Frames()) {
            index++;  // the frame's real thumbnail, if the replay has them

            if (!gameRunning_) {
                break;
            }

            const double arrival = start_ + frame.arrival;

            if (arrival < Now() + 0.002) {
                continue;  // before the tool switched the mirror on
            }

            const double target = frame.slot + frame.lag * period_;
            Quat pose;

            if (!replay_.PredictionFor(target, period_, &pose)) {
                pose = TrueHead(start_ + target);
            }

            WaitUntil(arrival - 0.0015);
            ResetEvent(event_.Get());
            Render(pose, index);
            WaitUntil(arrival);
            SetEvent(event_.Get());

            const double t = Now();
            std::lock_guard<std::mutex> lock(logMutex_);

            if (log_) {
                fprintf(
                    log_,
                    "frame,%lld,%.6f,%.6f,%.6f,%.7f,%.7f,%.7f,%.7f\n",
                    frame.serial,
                    arrival - 0.0015,
                    start_ + target,
                    t,
                    pose.x,
                    pose.y,
                    pose.z,
                    pose.w
                );
            }
        }

        timeEndPeriod(1);
    }

    void Headset::PoseLogLoop() {
        timeBeginPeriod(1);

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

        timeEndPeriod(1);
    }

}
