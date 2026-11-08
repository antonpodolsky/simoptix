// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// PvrMirror: the Pimax compositor's client mirror, the game's own left-eye image (a half-size
// copy) as a texture shared with us, and its frame signal, an event in pi_server we duplicate.
// Switching it on and off, opening the texture (on the hosts' copy device) and the signal, telling a
// new frame without consuming the signal, and the wait on it while no game runs. It never touches
// the game process. Knows nothing about poses, frame timing or what the hosts do with the image.
//
// The texture is opened on the hosts' copy thread; the signal is read by the poll thread. The
// signal's handle is guarded by the session's call lock (methods taking PvrSession::Calls).
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>

#include "common/footprint.h"
#include "shared/unique_handle.h"
#include "vr_mirror/frame_sources/pimax/pvr_session.h"

class PvrMirror {
public:
    ~PvrMirror();

    // Switches the mirror on or off in the runtime (on: the server creates its buffer).
    void Enable(PvrSession::Calls& calls, bool on);
    // Last-resort cleanup from crash/exit handlers: just switch the mirror off.
    static void EmergencyDisable();

    // Opens (or re-opens, if the server re-created it) the shared texture, and the frame signal if
    // it is missing or its handle changed. `config`: the config reads, for the footprint stats.
    bool OpenTexture(ID3D11Device* device, PvrSession& session, CallStats& config);
    ID3D11Texture2D* Texture() const { return texture_.Get(); }
    bool HasTexture() const { return hasTexture_; }
    // Drops the texture (Stop).
    void ReleaseTexture();
    // After the session was re-created: the next OpenTexture reopens even unchanged handles (Windows
    // reuses handle values, and the old buffer is abandoned).
    void ReopenOnNextOpen() { reopen_ = true; }

    // (Re)opens the frame signal by duplicating the server's event. logFailures: false when retrying
    // the same handles, so a lasting failure is logged once.
    void OpenSignal(PvrSession& session, CallStats& config, bool logFailures = true);
    void CloseSignal(PvrSession::Calls& calls);
    bool HasSignal(const PvrSession::Calls&) const { return static_cast<bool>(syncEvent_); }
    // Poll thread: whether the server signalled a new frame since the last call. An auto-reset event
    // is consumed here (we are its reader); a manual-reset one is edge-detected.
    bool NewFrame(PvrSession::Calls& calls);
    // After the session was re-created: a manual-reset signal's next high counts as a frame.
    void ResetEdge(PvrSession::Calls&) { prevState_ = 0; }

    // Poll thread, while no game runs: waits up to `ms` for the next frame on a duplicate of the
    // signal (so it never holds the call lock, and OpenSignal may replace the signal meanwhile).
    // True on a frame; `consumed`: the wait took an auto-reset signal, so that frame counts as new.
    bool WaitForFrame(PvrSession& session, DWORD ms, bool* consumed);
    void EndWait();  // closes the duplicate (the game is back, or the poll thread ends)

private:
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;  // copy thread
    int64_t textureHandle_ = 0;                        // copy thread
    std::atomic<bool> hasTexture_{false};
    std::atomic<bool> reopen_{false};
    // Under the call lock: the duplicated signal, the server's handle value it came from, and a
    // number that changes whenever it is closed or reopened.
    UniqueHandle syncEvent_;
    int64_t syncHandle_ = 0;
    int syncGeneration_ = 0;
    LONG prevState_ = 0;           // poll thread: a manual-reset signal's last state
    UniqueHandle idleWait_;        // poll thread: WaitForFrame's duplicate
    int idleWaitGeneration_ = -1;  // ...and the syncGeneration_ it was made at
};
