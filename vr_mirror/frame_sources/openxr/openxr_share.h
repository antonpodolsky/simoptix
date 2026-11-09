// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The memory the OpenXR layer shares with VR Mirror: what the layer publishes about each frame the
// game hands the headset, where its copy of the left eye's picture is, and the little the reader tells
// the layer back. The layer (openxr_publisher.*) writes it, the OpenXR frame source in VR Mirror
// (openxr_source.*) reads it; the e2e tests' fake game writes it with the layer's own code.
//
// The layer makes it when the game's D3D11 session starts, as a named file mapping in this Windows
// session (kMappingName), and sets the event kNewPictureEvent each time a copy of a picture is finished;
// a reader sets kReaderEvent when it starts, and the layer sets a waiting reader's kSessionEvent.
// Only plain data and fixed sizes: no pointers, no OpenXR or D3D types, so both sides (64-bit) agree by
// construction; kVersion changes with every change of layout or meaning.
//
// Each frame (Record), in order: the layer's frame number, the display time the game drew it for, the
// two eyes' poses and fields of view as the game passed them, which of the kTextures shared textures
// holds the left eye's picture (-1: none, the frame was not copied), and how the game handed that image
// over (which swapchain image, on which threads). A record is written under a sequence
// count (odd while it is written), so a reader never takes half of one. The picture in texture k is
// frame `textureFrame[k]`'s once its copy is finished on the GPU; 0 while one is being copied in.
//
// Who may touch a texture: the layer copies into texture k only while no reader reads it (readerBusy's
// count for k is 0; several readers can read at once: two OBS sources, or OBS and the layer's check),
// and a reader reads texture k only while textureFrame[k] still names the frame it wants. Each side first
// writes its own mark (the layer: textureFrame[k] = 0; a reader: its count up), then looks at the
// other's (all sequentially consistent), so at least one of them sees the other and backs off: the layer
// skips that frame's copy, or the reader skips that frame. Nobody waits.
//
// Times: XrTime (nanoseconds on the runtime's clock), as the game passes them. The clock reading (the
// runtime's time converted from a performance-counter moment, between two counter readings) lets the
// reader put them on the performance counter.
#pragma once

#include <atomic>
#include <cstdint>

namespace openxr_share {

    constexpr const wchar_t* kMappingName = L"Local\\SimOptix.OpenXR.FrameSource";
    constexpr const wchar_t* kNewPictureEvent = L"Local\\SimOptix.OpenXR.FrameSource.NewPicture";
    // Set by a reader when it starts reading, so the layer makes its textures at once rather than at its
    // thread's next tick (half a second).
    constexpr const wchar_t* kReaderEvent = L"Local\\SimOptix.OpenXR.FrameSource.Reader";
    // Made by a reader waiting for the game (manual reset), set by the layer when the game's session starts,
    // so the reader opens the memory at once rather than at its next look (once a second).
    constexpr const wchar_t* kSessionEvent = L"Local\\SimOptix.OpenXR.FrameSource.Session";
    constexpr uint32_t kMagic = 0x46584F53;  // "SOXF"
    constexpr uint32_t kVersion = 2;
    constexpr int kTextures = 3;
    constexpr int kRecords = 256;  // about 2.8 s of frames at 90 Hz

    // The reader counts as there while it signs within this long (performance-counter seconds); it signs
    // twice a second.
    constexpr double kReaderTimeoutS = 1.5;
    // The layer's textures are let go after the reader has been gone this long (seconds).
    constexpr double kReleaseAfterS = 10.0;
    // The layer signs once a second; a reader takes it as gone after this long without a sign (seconds).
    constexpr double kLayerTimeoutS = 3.0;

    // OpenXR's pose: the orientation (x, y, z, w) and position (metres) in the game's reference space
    // (right-handed, +Y up, -Z forward).
    struct Pose {
        float qx, qy, qz, qw;
        float px, py, pz;
    };

    // OpenXR's field of view: the angles of the four edges from the view's axis (radians; left and down
    // negative).
    struct Fov {
        float left, right, up, down;
    };

    struct Record {
        std::atomic<uint64_t> seq;  // 2 * frame once written; odd while it is written
        uint64_t frame;             // the layer's count of frames in this session, from 1
        int64_t displayTime;        // XrTime (ns) the game drew it for (XrFrameEndInfo::displayTime)
        int64_t endFrameCounter;    // performance counter when xrEndFrame returned to the layer
        Pose eye[2];                // the projection layer's views 0 (left) and 1 (right), as passed
        Fov fov[2];
        int32_t texture;  // the shared texture its picture is copied into; -1: not copied
        // the part of the game's image copied (pixels); the texture holds it at 0, 0
        int32_t x, y, width, height;

        // How the game handed the left eye's image over, for the frame trace.
        int32_t gameImage;    // the swapchain image the frame shows; -1: none known
        uint32_t imagesHeld;  // that swapchain's images the game held acquired as it ended the frame
        uint32_t
            releasedDuringEnd;   // that swapchain's images it released while xrEndFrame was in the runtime
        uint32_t endThread;      // the game's thread that ended the frame (Windows thread id)
        uint32_t releaseThread;  // the game's thread that released the image
    };

    enum ContextProtection : uint32_t {
        kProtectionUnknown = 0,
        kUnprotected = 1,  // the game's device context is not safe for two threads at once
        kProtected = 2,    // it is: the game turned D3D11's multithread protection on
    };

    enum State : uint32_t {
        kRunning = 1,  // the game's session runs
        kEnded = 2,    // it ended: let go of everything
    };

    struct Header {
        uint32_t magic;                 // kMagic
        uint32_t version;               // kVersion
        uint32_t size;                  // sizeof(Header)
        uint32_t processId;             // the game's (for the log only)
        std::atomic<uint32_t> session;  // changes with each session the layer starts (the frames count anew)
        std::atomic<uint32_t> state;    // State
        uint32_t adapterLow;            // the game's graphics adapter (LUID), for the reader's device
        int32_t adapterHigh;
        int64_t counterFrequency;  // performance-counter ticks per second, as the layer read it
        // ContextProtection of the game's device context, which the layer copies on from the thread that
        // ends the frame
        uint32_t contextProtection;

        // The shared textures (legacy D3D11 shared handles), made while a reader is there.
        std::atomic<uint32_t> textureGeneration;  // changes each time they are made; 0: there are none
        uint32_t textureWidth, textureHeight;     // pixels
        uint32_t textureFormat;                   // DXGI_FORMAT of the shared textures
        uint32_t gameFormat;                      // DXGI_FORMAT of the game's swapchain (its colour meaning)
        uint64_t textureHandle[kTextures];
        std::atomic<uint64_t> textureFrame[kTextures];  // the frame whose picture texture k holds; 0: none

        // The reader's side.
        std::atomic<int64_t> readerSigned;  // performance counter of the reader's last sign; 0: no reader
        std::atomic<uint32_t> readerBusy;   // byte k: how many readers are reading texture k (ReaderCount)

        // The layer's side, besides the frames.
        std::atomic<int64_t> layerSigned;    // performance counter of the layer's last sign
        std::atomic<int64_t> displayPeriod;  // ns, from xrWaitFrame; 0 unknown
        std::atomic<uint32_t> clockSeq;      // the clock reading's sequence count (odd while written)
        int64_t clockCounterBefore, clockCounterAfter;  // performance counter around...
        int64_t clockTime;  // ...the runtime's time for the moment between them (XrTime, ns)

        std::atomic<uint64_t> frames;  // records written: the newest is records[(frames - 1) % kRecords]
        Record records[kRecords];
    };

    static_assert(std::atomic<uint64_t>::is_always_lock_free, "the shared memory needs lock-free atomics");
    static_assert(std::atomic<int64_t>::is_always_lock_free, "the shared memory needs lock-free atomics");
    static_assert(std::atomic<uint32_t>::is_always_lock_free, "the shared memory needs lock-free atomics");

    // ---- The reader's side of the rules above, for every reader (VR Mirror, the layer's check). ----

    // Frame `frame`'s record, if it is still there whole (not yet written over by a frame kRecords later).
    inline bool ReadRecord(const Header& h, uint64_t frame, Record* out) {
        const Record& r = h.records[(frame - 1) % kRecords];
        const uint64_t before = r.seq.load(std::memory_order_acquire);
        if (before != 2 * frame) {
            return false;
        }
        out->frame = r.frame;
        out->displayTime = r.displayTime;
        out->endFrameCounter = r.endFrameCounter;
        for (int e = 0; e < 2; e++) {
            out->eye[e] = r.eye[e], out->fov[e] = r.fov[e];
        }
        out->texture = r.texture;
        out->x = r.x, out->y = r.y, out->width = r.width, out->height = r.height;
        out->gameImage = r.gameImage, out->imagesHeld = r.imagesHeld;
        out->releasedDuringEnd = r.releasedDuringEnd;
        out->endThread = r.endThread, out->releaseThread = r.releaseThread;
        std::atomic_thread_fence(std::memory_order_acquire);
        return r.seq.load(std::memory_order_relaxed) == before && out->frame == frame;
    }

    // The latest clock reading: the runtime's time `time` (ns) at performance-counter moment `counter`.
    inline bool ReadClock(const Header& h, int64_t* counter, int64_t* time) {
        const uint32_t before = h.clockSeq.load(std::memory_order_acquire);
        if (before == 0 || (before & 1)) {
            return false;
        }
        const int64_t c0 = h.clockCounterBefore, c1 = h.clockCounterAfter, t = h.clockTime;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (h.clockSeq.load(std::memory_order_relaxed) != before) {
            return false;
        }
        *counter = c0 + (c1 - c0) / 2;
        *time = t;
        return true;
    }

    // How many readers are reading texture k, from readerBusy.
    inline uint32_t ReaderCount(uint32_t busy, int k) {
        return (busy >> (8 * k)) & 0xFF;
    }

    // Before reading texture k for frame `frame`: counts this reader in, then checks the texture still holds
    // that frame's finished copy. False (and counted out again): it doesn't, skip the frame.
    inline bool ClaimTexture(Header& h, int k, uint64_t frame) {
        h.readerBusy.fetch_add(1u << (8 * k));
        if (h.textureFrame[k].load() == frame) {
            return true;
        }
        h.readerBusy.fetch_sub(1u << (8 * k));
        return false;
    }

    // Once this reader's GPU has finished reading texture k.
    inline void ReleaseTexture(Header& h, int k) {
        h.readerBusy.fetch_sub(1u << (8 * k));
    }

}
