// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "vr_mirror/frame_sources/pimax/pvr_mirror.h"

#include <tlhelp32.h>

#include "shared/log.h"

namespace {

    // Runtime config keys and handle names of the client mirror, taken from strings in pi_server.exe:
    // the game's own submitted left-eye image (2508x2084 on the Dream Air at Render Quality 1.0), one
    // buffer with one frame signal.
    constexpr const char* kEnableKey = "enable_client_mirror";
    constexpr const char* kTextureKey = "client_mirror_handle";
    constexpr const char* kSyncKey = "client_mirror_sync_handle";

    // The runtime, and the session's headset the mirror is on in (null while it is off).
    std::atomic<const PvrInterface*> g_emergencyRuntime{nullptr};
    std::atomic<PvrHmd> g_emergencyHmd{nullptr};

    // Reads an event's state without waiting on it, so an auto-reset event is never consumed.
    using NtQueryEventFn = LONG(NTAPI*)(HANDLE, int, PVOID, ULONG, PULONG);
    struct EventBasicInfo {
        int type;  // 0 = notification (manual reset), 1 = synchronization (auto reset)
        LONG state;
    };

    NtQueryEventFn NtQueryEvent() {
        static auto fn = reinterpret_cast<NtQueryEventFn>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryEvent")
        );

        return fn;
    }

    bool QueryEvent(HANDLE h, EventBasicInfo* info) {
        auto fn = NtQueryEvent();
        return fn && fn(h, 0, info, sizeof(*info), nullptr) >= 0;
    }

    DWORD FindProcess(const wchar_t* exeName) {
        UniqueHandle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        if (!snap) {
            return 0;
        }
        PROCESSENTRY32W pe = {sizeof(pe)};
        DWORD pid = 0;
        for (BOOL ok = Process32FirstW(snap.Get(), &pe); ok; ok = Process32NextW(snap.Get(), &pe)) {
            if (_wcsicmp(pe.szExeFile, exeName) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        }

        return pid;
    }

}

PvrMirror::~PvrMirror() = default;

void PvrMirror::Enable(PvrSession::Calls& calls, bool on) {
    const int r = calls.SetIntConfig(kEnableKey, on ? 1 : 0);
    if (on) {
        CoreLog("%s=1 -> %d\n", kEnableKey, r);
    } else {
        CoreLog("%s=0\n", kEnableKey);
    }

    if (on) {
        g_emergencyRuntime = calls.Runtime();
    }
    g_emergencyHmd = on ? calls.Hmd() : nullptr;
}

void PvrMirror::EmergencyDisable() {
    const PvrHmd hmd = g_emergencyHmd.load();
    const PvrInterface* pvr = g_emergencyRuntime.load();
    if (hmd && pvr) {
        pvr->setIntConfig(hmd, kEnableKey, 0);
    }
}

bool PvrMirror::OpenTexture(ID3D11Device* device, PvrSession& session, CallStats& config) {
    int64_t handle = 0;

    // The frame signal is checked on its own: it can be missing when the texture is not (the
    // server hadn't published it yet when we opened it), or be replaced while the texture's
    // handle value stays the same. Either way no frame would ever arrive.
    bool syncChanged = false, syncMissing = false;

    {
        PvrSession::Calls calls(session);
        if (!calls.HasSession()) {
            return texture_ != nullptr;  // reconnecting
        }
        handle = calls.Int64Config(kTextureKey, config);
        const int64_t sync = calls.Int64Config(kSyncKey, config);
        if (sync) {  // else not published (yet): nothing to open
            syncChanged = sync != syncHandle_;
            syncMissing = !syncEvent_;
        }
    }

    // After a reconnect the new session's buffer can come back with the same handle value
    // (Windows reuses them): reopen regardless, or we keep reading the old, abandoned texture.
    const bool force = reopen_.exchange(false);
    bool changed = false;
    if (force || handle != textureHandle_ || !texture_) {
        texture_.Reset();
        textureHandle_ = handle;
        if (handle != 0 && handle != -1) {
            HRESULT hr = device->OpenSharedResource(
                reinterpret_cast<HANDLE>(handle),
                IID_PPV_ARGS(&texture_)
            );

            CoreLog("open mirror buffer 0 (0x%llx): 0x%08lx\n", static_cast<unsigned long long>(handle), hr);
            changed = true;
        }

        hasTexture_ = texture_ != nullptr;
    }

    if (changed || syncChanged) {
        if (syncChanged && !changed) {
            CoreLog("frame signal changed on the server: reopening it\n");
        }
        OpenSignal(session, config);
    } else if (syncMissing) {
        OpenSignal(session, config, false);  // retry every call (2 s) until it opens
    }

    return texture_ != nullptr;
}

void PvrMirror::ReleaseTexture() {
    texture_.Reset();
    hasTexture_ = false;
}

void PvrMirror::OpenSignal(PvrSession& session, CallStats& config, bool logFailures) {
    PvrSession::Calls calls(session);
    CloseSignal(calls);
    if (!calls.HasSession()) {
        return;  // reconnecting
    }

    // Recorded also when opening fails below: OpenTexture then retries it quietly.
    syncHandle_ = calls.Int64Config(kSyncKey, config);

    // The process that owns the sync events. SIMOPTIX_VR_MIRROR_SERVER_EXE lets the e2e tests point
    // this at a fake runtime; normally it is Pimax's compositor.
    wchar_t serverExe[MAX_PATH] = L"pi_server.exe";
    GetEnvironmentVariableW(L"SIMOPTIX_VR_MIRROR_SERVER_EXE", serverExe, MAX_PATH);
    DWORD serverPid = FindProcess(serverExe);
    UniqueHandle server(serverPid ? OpenProcess(PROCESS_DUP_HANDLE, FALSE, serverPid) : nullptr);
    if (!server) {
        if (logFailures) {
            CoreLog(
                "cannot open pi_server for handle duplication (%lu); buffer sync disabled\n",
                GetLastError()
            );
        }

        return;
    }

    const int64_t h = syncHandle_;
    if (!h
        || !DuplicateHandle(
            server.Get(),
            reinterpret_cast<HANDLE>(h),
            GetCurrentProcess(),
            syncEvent_.Receive(),
            0,
            FALSE,
            DUPLICATE_SAME_ACCESS
        )) {
        if (logFailures) {
            CoreLog(
                "sync event 0 (0x%llx) not available (%lu)\n",
                static_cast<unsigned long long>(h),
                GetLastError()
            );
        }

        syncEvent_.Reset();
    } else {
        EventBasicInfo info = {};
        const bool ok = QueryEvent(syncEvent_.Get(), &info);
        CoreLog("sync event 0: %s type=%d state=%ld\n", ok ? "ok" : "query failed", info.type, info.state);
    }
}

void PvrMirror::CloseSignal(PvrSession::Calls&) {
    syncEvent_.Reset();
    syncGeneration_++;
    syncHandle_ = 0;
}

bool PvrMirror::NewFrame(PvrSession::Calls&) {
    EventBasicInfo info = {};
    if (!syncEvent_ || !QueryEvent(syncEvent_.Get(), &info)) {
        return false;
    }
    if (info.type == 1) {
        return WaitForSingleObject(syncEvent_.Get(), 0) == WAIT_OBJECT_0;
    }
    const bool edge = info.state && !prevState_;
    prevState_ = info.state;
    return edge;
}

bool PvrMirror::WaitForFrame(PvrSession& session, DWORD ms, bool* consumed) {
    *consumed = false;
    {
        PvrSession::Calls calls(session);
        if (idleWaitGeneration_ != syncGeneration_) {
            EndWait();
            if (syncEvent_) {
                DuplicateHandle(
                    GetCurrentProcess(),
                    syncEvent_.Get(),
                    GetCurrentProcess(),
                    idleWait_.Receive(),
                    0,
                    FALSE,
                    DUPLICATE_SAME_ACCESS
                );
            }

            idleWaitGeneration_ = syncGeneration_;
        }
    }

    // A manual-reset signal is low while a frame is written and high once it is done; with no
    // game it stays low (measured), so waiting for it to go high waits for the next frame. If
    // it was left high, first look for it to go low (a frame being written) every pass.
    // An auto-reset one is consumed by the wait, so that frame is passed on (consumed).
    bool frame = false;
    EventBasicInfo info = {};
    DWORD waited = WAIT_FAILED;

    if (idleWait_ && QueryEvent(idleWait_.Get(), &info) && (info.type == 1 || !info.state)) {
        if (info.type != 1) {
            prevState_ = 0;
        }
        waited = WaitForSingleObject(idleWait_.Get(), ms);
        frame = waited == WAIT_OBJECT_0;
        *consumed = frame && info.type == 1;
    }

    if (waited == WAIT_FAILED) {
        Sleep(ms);  // no signal to wait on, or it stayed high
    }
    if (frame) {
        EndWait();
    }
    return frame;
}

void PvrMirror::EndWait() {
    idleWait_.Reset();
    idleWaitGeneration_ = -1;
}
