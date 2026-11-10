// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "vr_mirror/frame_sources/pimax/pvr_session.h"

#include "shared/log.h"

namespace {

    // Pimax's client library, loaded once and kept loaded until Close, also after a failed initialise
    // (the next Open then tries again with it).
    HMODULE g_clientDll = nullptr;
    const PvrInterface* g_pvr = nullptr;

    PvrResult LoadClient() {
        if (g_pvr) {
            return kPvrSuccess;
        }
        HMODULE dll = LoadLibraryA(kPvrClientDll);
        if (!dll) {
            return kPvrDllFailed;
        }
        auto getInterface = reinterpret_cast<PvrGetInterface>(GetProcAddress(dll, kPvrGetInterfaceExport));
        if (!getInterface) {
            FreeLibrary(dll);
            return kPvrDllWrong;
        }

        const PvrInterface* pvr = getInterface(kPvrInterfaceMajor, kPvrInterfaceMinor);
        if (!pvr) {
            FreeLibrary(dll);
            return kPvrInterfaceNotFound;
        }

        g_clientDll = dll;
        g_pvr = pvr;
        return kPvrSuccess;
    }

    void UnloadClient() {
        if (g_pvr) {
            g_pvr->shutdown();
        }
        if (g_clientDll) {
            FreeLibrary(g_clientDll);
        }
        g_clientDll = nullptr;
        g_pvr = nullptr;
    }

    Quat ToQuat(const PvrQuat& q) {
        return {q.x, q.y, q.z, q.w};
    }

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

    // Runs one runtime call and adds its duration to `stats`.
    template <class F>
    auto Timed(CallStats& stats, F&& call) -> decltype(call()) {
        const double t0 = QpcSeconds();
        auto result = call();
        stats.Add(QpcSeconds() - t0);
        return result;
    }

}

bool PvrSession::Open(std::string* error) {
    PvrResult r = LoadClient();
    if (r == kPvrSuccess) {
        r = g_pvr->initialise();
    }
    if (r != kPvrSuccess) {
        *error = "Could not load the Pimax runtime (" + std::to_string(r) + "). Is Pimax Play running?";
        return false;
    }

    pvr_ = g_pvr;
    PvrHmd hmd = nullptr;
    r = pvr_->createHmd(&hmd);
    if (r != kPvrSuccess) {
        *error = "Could not open a PVR session (" + std::to_string(r) + ").";
        UnloadClient();
        pvr_ = nullptr;
        return false;
    }

    hmd_ = hmd;

    PvrDisplayInfo disp = {};
    pvr_->getEyeDisplayInfo(hmd, kPvrEyeLeft, &disp);
    luid_ = disp.adapterLuid;
    if (disp.refreshRate > 1) {
        period_ = 1.0 / disp.refreshRate;
    }
    CoreLog("display: %.3f Hz\n", disp.refreshRate);

    for (int eye = 0; eye < 2; eye++) {
        PvrEyeRenderInfo info = {};
        if (pvr_->getEyeRenderInfo(hmd, eye == 0 ? kPvrEyeLeft : kPvrEyeRight, &info) == kPvrSuccess) {
            // The client mirror holds the game's image sRGB-encoded twice.
            eyes_[eye] = {
                info.fov.leftTan,
                info.fov.rightTan,
                info.fov.upTan,
                info.fov.downTan,
                ToQuat(info.hmdToEye.orientation),
                true
            };
        }

        const EyeInfo& e = eyes_[eye];
        CoreLog(
            "eye %d: tan L%.3f R%.3f U%.3f D%.3f, cant q=(%.4f %.4f %.4f %.4f)\n",
            eye,
            e.leftTan,
            e.rightTan,
            e.upTan,
            e.downTan,
            e.hmdToEye.x,
            e.hmdToEye.y,
            e.hmdToEye.z,
            e.hmdToEye.w
        );
    }

    return true;
}

void PvrSession::Close() {
    {
        std::lock_guard<std::mutex> calls(mutex_);
        DestroySession();
    }

    if (pvr_) {
        UnloadClient();
        pvr_ = nullptr;
    }
}

bool PvrSession::CreateSession() {
    PvrHmd hmd = nullptr;
    if (pvr_->createHmd(&hmd) != kPvrSuccess) {
        return false;
    }
    std::lock_guard<std::mutex> calls(mutex_);
    hmd_ = hmd;
    return true;
}

void PvrSession::DestroySession() {
    if (!hmd_) {
        return;
    }
    pvr_->destroyHmd(hmd_);
    hmd_ = nullptr;
}

double PvrSession::Time(CallStats& stats) {
    std::lock_guard<std::mutex> calls(mutex_);
    if (!pvr_) {
        return 0;
    }
    return Timed(stats, [&] { return pvr_->getTimeSeconds(); });
}

double PvrSession::Calls::Time(CallStats& stats) {
    if (!s_.pvr_) {
        return 0;
    }
    return Timed(stats, [&] { return s_.pvr_->getTimeSeconds(); });
}

double PvrSession::Calls::Time(double* qpcBefore, double* qpcAfter) {
    *qpcBefore = QpcSeconds();
    const double t = s_.pvr_ ? s_.pvr_->getTimeSeconds() : 0;
    *qpcAfter = QpcSeconds();
    return t;
}

bool PvrSession::Calls::Orientation(double t, Quat* q, CallStats& stats) {
    if (!s_.hmd_) {
        return false;
    }
    PvrTrackingState st = {};
    const bool ok = Timed(stats, [&] { return s_.pvr_->getTrackingState(s_.hmd_, t, &st); }) == kPvrSuccess
                    && (st.head.statusFlags & kPvrOrientationTracked);

    *q = ToQuat(st.head.pose.orientation);
    return ok;
}

double PvrSession::Calls::NextDisplaySlot(CallStats& stats) {
    if (!s_.hmd_) {
        return 0;
    }
    return Timed(stats, [&] { return s_.pvr_->getPredictedDisplayTime(s_.hmd_, 0); });
}

bool PvrSession::Calls::ShouldQuit(CallStats& stats) {
    if (!s_.hmd_) {
        return false;
    }
    PvrHmdStatus status = {};
    return Timed(stats, [&] { return s_.pvr_->getHmdStatus(s_.hmd_, &status); }) == kPvrSuccess
           && status.shouldQuit;
}

int64_t PvrSession::Calls::Int64Config(const char* key, CallStats& stats) {
    if (!s_.hmd_) {
        return 0;
    }
    return Timed(stats, [&] { return s_.pvr_->getInt64Config(s_.hmd_, key, 0); });
}

int PvrSession::Calls::SetIntConfig(const char* key, int value) {
    if (!s_.hmd_) {
        return -1;
    }
    return s_.pvr_->setIntConfig(s_.hmd_, key, value);
}
