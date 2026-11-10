// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// SimOptix: VR Mirror, the mirror app: shows a stabilized view of the headset in a desktop window,
// drawn once per game frame. This file is only the window host; the frame sources (..\frame_sources\:
// the Pimax game eye, or the OpenXR layer's image), smoothing and warp math are shared with the OBS
// plugin. Its settings come from simoptix_vr_mirror.ini (mirror_app_settings.h); it has no controls.

#include <windows.h>
#include <d3d11.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "adapters/d3d/drawn_pose_feed.h"
#include "adapters/d3d/frame_copy.h"
#include "common/settings.h"
#include "frame_hold.h"
#include "mirror_app_settings.h"
#include "pipeline/pipeline_builder.h"
#include "precise_wait.h"
#include "shared/guarded.h"
#include "shared/log.h"
#include "vr_mirror/frame_sources/openxr/openxr_source.h"
#include "vr_mirror/frame_sources/pimax/pimax_source.h"
#include "window_output.h"

#ifndef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
#define PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION 0x4  // Windows 11; newer than the SDK in use
#endif

namespace {

    // Each frame is presented this long after its de-jittered arrival time instead of right away (ms).
    // OBS grabs the newest present on its own 90 Hz clock, which drifts against the headset's; presents
    // that wobble by +-2 ms around OBS's grab time make it repeat and skip frames for minutes at a time.
    // Steady presents shrink that to one repeat per clock slip.
    constexpr double kPresentDelayMs = 4;

    struct App {
        HWND hwnd = nullptr;
        MirrorAppSettings settings;
        std::unique_ptr<FrameSource> source;  // the frames and poses (MirrorAppSettings::frameSource)
        FrameCopy frame;
        FrameHold
            hold;  // look-ahead smoothing (settings smoothing 2, 3): frames held until their view is known
        Pipeline pipeline;     // stages 2-6 (vr_mirror\core\pipeline), built for the settings at start
        DrawnPoseFeed feed;    // feeds the pipeline's drawn pose the frames' images
        FrameTimes lastTimes;  // the frame last drawn: its times

        WindowOutput output;   // the device, swap chain and warp shader
        PreciseWait wait;      // paced presents and naps on a high-resolution timer
        int latePresents = 0;  // frames whose pace time had already passed when drawn
    };

    App* g_app = nullptr;

    LONG WINAPI CrashHandler(EXCEPTION_POINTERS*) {
        PimaxSource::EmergencyDisable();
        return EXCEPTION_CONTINUE_SEARCH;
    }

    BOOL WINAPI ConsoleHandler(DWORD) {
        PimaxSource::EmergencyDisable();
        return FALSE;
    }

    // The device on the headset's adapter, the window's swap chain and shader, and the drawn pose's
    // images on that device.
    bool CreateDevice(App& app) {
        if (!app.output.Create(app.hwnd, app.source->AdapterLuid())) {
            return false;
        }
        app.feed.Init(app.output.Device());
        return true;
    }

    // How large the window shows the images: the frame source keeps them that size, if it can.
    void SetImageNeed(App& app) {
        const float aspect = float(app.output.Width()) / float(std::max(1, app.output.Height()));
        const int held = std::max(1, app.hold.Capacity());
        app.source->SetImageNeed(
            NativeImageWidth(app.source->Eye(), app.settings.core, app.output.Width(), aspect),
            kImageMemory / held
        );
    }

    // Hands the drawn pose the images the GPU has finished.
    void PollFeed(App& app) {
        app.feed.Poll(
            app.output.Context(),
            app.source->RecordedPredictions(),
            app.source->Eye(),
            app.source->Period()
        );

        app.source->PollImages(app.output.Context());
    }

    // A line of the stages' shared report (StatsReport), on the console.
    void PrintLine(const char* line) {
        printf("%s\n", line);
    }

    // Every stage's numbers since the last call, and the time looking at images with patches.
    void PrintStats(App& app) {
        LineStatsReport<void (*)(const char*)> out(PrintLine);
        app.pipeline.Report(out);
        DrawnPoseFeed::ReportLookTime(app.feed.TakeLookTime(), out);
    }

    // The title shows the settings and the state. (OBS window capture records only the inside of the
    // window, never the title.)
    void UpdateTitle(App& app, const GameFrame& f) {
        const MirrorSettings& s = app.settings.core;
        const wchar_t* const kSmoothing[] = {L"off", L"instant", L"look-ahead"};
        wchar_t title[256];
        swprintf_s(
            title,
            L"SimOptix: VR Mirror  |  smoothing %s, zoom %.2f  |  %.0f fps%s",
            kSmoothing[std::clamp(s.smoothing, 0, 2)],
            s.zoom,
            app.source->SourceFps(),
            f.buffer < 0         ? L"  |  waiting for headset"
            : app.source->Idle() ? L"  |  no game frames (idle)"
            : f.synced           ? L""
                                 : L"  |  unsynced"
        );

        SetWindowTextW(app.hwnd, title);
    }

    // Draws `image` (game frame `shown`) warped by the pipeline, then presents at `presentAt` if that is
    // set and still ahead, otherwise immediately. `image` null: just the background. `f` is the newest
    // frame, for the title.
    void Draw(
        App& app,
        ID3D11ShaderResourceView* image,
        const GameFrame& shown,
        const GameFrame& f,
        double now,
        double presentAt
    ) {
        const MirrorSettings& s = app.settings.core;
        app.lastTimes = TimesOf(shown);
        WarpParams p = {};
        if (image) {
            const float aspect = float(app.output.Width()) / float(std::max(1, app.output.Height()));
            p = app.pipeline.Update(s, *app.source, app.source->Eye(), {app.lastTimes, nullptr, now, aspect});
        }

        app.output.Draw(image, p);
        if (presentAt > 0) {
            const double now2 = app.source->Now();
            if (now2 >= presentAt) {
                app.latePresents++;
            } else if (presentAt - now2 < 0.05) {
                app.wait.Until(*app.source, presentAt);  // never hold a frame for long (clock glitch)
            }
        }

        app.output.Present();
        if (image) {
            app.pipeline.Presented(app.source->Now());  // for the frame trace
        }

        static double lastTitle = 0;
        if (now - lastTitle > 0.5) {
            lastTitle = now;
            UpdateTitle(app, f);
        }
    }

    // Instant smoothing: copies and draws the frame right away (the game reuses its buffer about a
    // frame later).
    void Render(App& app, const GameFrame& f, double now, double presentAt) {
        if (f.buffer < 0
            || !app.source->CopyImage(app.output.Device(), app.output.Context(), f, &app.frame, false)) {
            Draw(app, nullptr, f, f, now, presentAt);
            return;
        }

        // The images the drawn pose looks at (if the game has frames queued, it warps from the slot
        // the frame was rendered for).
        PollFeed(app);
        const float whole[4] = {0, 0, 1, 1};
        app.feed.Submit(
            app.output.Context(),
            app.frame.View(),
            whole,
            f,
            app.source->RecordedPredictions(),
            app.source->Eye(),
            app.source->Period()
        );

        if (app.feed.Measuring(f.serial)) {
            // A drawn pose that measures this frame's own patches (the cockpit correction, while frames
            // arrive irregularly) has them about a millisecond after the copy, as the OBS plugin's frames
            // have theirs before it draws them (a frame drawn with the last frame's correction keeps its
            // own jump). Never long.
            app.output.Context()->Flush();
            const double until = app.source->Now() + 0.003;
            while (app.feed.Measuring(f.serial) && app.source->Now() < until) {
                app.wait.Nap(0.0002);
                PollFeed(app);
            }
        }

        Draw(app, app.frame.View(), f, f, now, presentAt);
    }

    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_GETMINMAXINFO: {
                // Allow a client area as large as the OBS canvas even on a monitor of the same size.
                auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
                mmi->ptMaxTrackSize = {8192, 8192};
                return 0;
            }

            case WM_SIZE:
                if (g_app && wp != SIZE_MINIMIZED) {
                    g_app->output.SetSize(std::max(1, int(LOWORD(lp))), std::max(1, int(HIWORD(lp))));
                    g_app->output.ResizeSwapChain();
                }

                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
        }

        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        return Guarded("window procedure", [&] { return HandleMessage(hwnd, msg, wp, lp); });
    }

    // This window is usually behind the game while recording. Windows 11 throttles such
    // processes: it ignores their 1 ms timer resolution (Sleep(1) becomes ~15 ms) and may run
    // them slower (EcoQoS). Both made the output miss its present times and freeze.
    void AvoidThrottling() {
        PROCESS_POWER_THROTTLING_STATE throttling = {};
        throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        throttling.ControlMask |= PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
        throttling.StateMask = 0;  // controlled and off: never throttle, always honor the timer resolution

        const BOOL optedOut = SetProcessInformation(
            GetCurrentProcess(),
            ProcessPowerThrottling,
            &throttling,
            sizeof(throttling)
        );

        if (!optedOut) {
            printf("could not opt out of background throttling (%lu)\n", GetLastError());
        }
    }

    // SIMOPTIX_VR_MIRROR_TRACE=<file>: the frame trace, one row per drawn frame (the pipeline's observer:
    // vr_mirror\core\pipeline\observer\stamp_trace_observer.h has the columns), for timing analysis, e.g. by
    // the e2e tests.
    FILE* OpenTrace(App& app) {
        wchar_t tracePath[MAX_PATH];

        if (!GetEnvironmentVariableW(L"SIMOPTIX_VR_MIRROR_TRACE", tracePath, MAX_PATH)) {
            return nullptr;
        }

        FILE* trace = nullptr;

        if (_wfopen_s(&trace, tracePath, L"w") != 0) {
            return nullptr;
        }

        app.settings.trace = true;
        return trace;
    }

    struct FileSink : TraceSink {
        FILE* file = nullptr;

        void Line(const char* line) override {
            fputs(line, file);
            fputc('\n', file);
        }
    };

    HWND CreateMirrorWindow(int width, int height) {
        WNDCLASSEXW windowClass = {sizeof(windowClass)};
        windowClass.lpfnWndProc = WndProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        windowClass.lpszClassName = L"SimOptixVrMirror";
        RegisterClassExW(&windowClass);

        RECT frame = {0, 0, width, height};
        AdjustWindowRect(&frame, WS_OVERLAPPEDWINDOW, FALSE);

        return CreateWindowExW(
            0,
            windowClass.lpszClassName,
            L"SimOptix: VR Mirror",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            frame.right - frame.left,
            frame.bottom - frame.top,
            nullptr,
            nullptr,
            windowClass.hInstance,
            nullptr
        );
    }

    struct FrameLoop {
        bool lookahead = false;
        double lastReopen = 0;
        double lastLateReport = 0;
        uint64_t lastHeld = 0;  // the newest frame copied into the hold
        int heldDropped = 0;    // frames dropped because the hold was full (logged every 10 s)

        // What the picker chooses from: the held frames, oldest first, then the newest frame.
        std::vector<StoredFrame> stored;
    };

    void KeepImagesCurrent(App& app, FrameLoop& loop, double now) {
        // Lets go of the source's images as soon as their copies are done (the OpenXR layer reuses its
        // textures two frames later and skips a copy into one still being read).
        app.source->PollImages(app.output.Context());

        // The source may re-create its images (Pimax when a VR app starts or stops, the OpenXR layer for
        // a new game session); pick up new handles. A source that can scale its images keeps them as
        // large as the window shows them (the eye's field of view can change with the game).
        if (now - loop.lastReopen > 2.0) {
            loop.lastReopen = now;
            SetImageNeed(app);
            app.source->OpenImages(app.output.Device());
        }
    }

    // No game frames: nothing new to draw. Wait for the next frame (or a window message)
    // instead of polling, and keep the title's status current.
    void WaitWhileIdle(App& app, double now) {
        HANDLE frameEvent = app.source->FrameEvent();
        MsgWaitForMultipleObjects(1, &frameEvent, FALSE, 500, QS_ALLINPUT);

        static double lastTitle = 0;

        if (now - lastTitle > 0.5) {
            lastTitle = now;
            UpdateTitle(app, app.source->LatestFrame());
        }
    }

    // Look-ahead smoothing: copy each new frame now (every one, in order); it is shown once its
    // view can be computed.
    void HoldNewFrames(App& app, FrameLoop& loop) {
        ID3D11DeviceContext* context = app.output.Context();
        const float whole[4] = {0, 0, 1, 1};
        GameFrame next;
        bool copied = false;

        while (app.source->NextFrame(loop.lastHeld, &next)) {
            loop.lastHeld = next.serial;

            FrameHold::Held* held = app.hold.Add(app.output.Device(), context, *app.source, next);

            if (!held) {
                loop.heldDropped++;
                continue;
            }

            app.feed.Submit(
                context,
                held->copy.View(),
                whole,
                next,
                app.source->RecordedPredictions(),
                app.source->Eye(),
                app.source->Period()
            );

            copied = true;
        }

        // One submission for the copies and their thumbnails (as the OBS plugin's copy thread): the
        // present that would carry them is an older frame's, up to a second later. Without it the slot
        // check's thumbnails waited for the GPU and 22% of frames went undecided (e2e).
        if (copied) {
            context->Flush();
        }

        PollFeed(app);
    }

    Pick PickFrame(App& app, FrameLoop& loop, const GameFrame& newest, bool hold, double now, int* total) {
        *total = 0;

        auto store = [&](const GameFrame& frame) {
            StoredFrame& stored = loop.stored[(*total)++];
            stored = {true, frame.serial, frame.time, frame.smoothTime, frame.slotTime, 0, frame.synced};
        };

        if (hold) {
            for (int k = 0; k < app.hold.Count(); k++) {
                store(app.hold.At(k).frame);
            }
        }

        store(newest);

        return app.pipeline.Picker()->Tick(now, app.source->Period(), loop.stored.data(), *total);
    }

    // A held frame (the oldest): Draw waits precisely and presents at its due time.
    void DrawHeld(App& app, FrameLoop& loop, double now, double due) {
        FrameHold::Held* held = app.hold.Front();
        Draw(app, held->copy.View(), held->frame, held->frame, now, due);
        app.hold.Pop();

        if (now - loop.lastLateReport < 10) {
            return;
        }

        if (app.latePresents > 0 || loop.heldDropped > 0) {
            printf(
                "held frames: %d drawn after their present time, %d dropped (hold full)\n",
                app.latePresents,
                loop.heldDropped
            );
        }

        PrintStats(app);
        app.latePresents = 0;
        loop.heldDropped = 0;
        loop.lastLateReport = now;
    }

    // The newest frame, copied and drawn right away (the game reuses its buffer about a frame later).
    void DrawNewest(App& app, FrameLoop& loop, const GameFrame& newest, double now, double due) {
        Render(app, newest, now, due);

        if (now - loop.lastLateReport < 10) {
            return;
        }

        if (app.latePresents > 0) {
            printf("%d frames were drawn after their paced present time\n", app.latePresents);
        }

        PrintStats(app);
        app.latePresents = 0;
        loop.lastLateReport = now;
    }

    void RunFrames(App& app, FrameLoop& loop) {
        MSG msg = {};

        while (msg.message != WM_QUIT) {
            if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
                continue;
            }

            const double now = app.source->Now();
            KeepImagesCurrent(app, loop, now);

            if (app.source->Idle()) {
                WaitWhileIdle(app, now);
                continue;
            }

            const GameFrame newest = app.source->LatestFrame();
            const bool hold = loop.lookahead && newest.synced;

            if (hold) {
                HoldNewFrames(app, loop);
            }

            int total = 0;
            const Pick pick = PickFrame(app, loop, newest, hold, now, &total);

            if (pick.kind == PickKind::kNone) {
                app.wait.Nap(0.0005);
                continue;
            }

            if (pick.index < total - 1) {
                DrawHeld(app, loop, now, pick.due);
            } else {
                DrawNewest(app, loop, newest, now, pick.due);
            }
        }
    }

}

int wmain() {
    SetUnhandledExceptionFilter(CrashHandler);
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    // Unbuffered output: when the log is redirected to a file, nothing is lost if the process ends abruptly.
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    AvoidThrottling();
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    App app;
    app.settings = LoadSettings();

    if (app.settings.frameSource == MirrorAppFrameSource::kOpenXr) {
        app.source = std::make_unique<OpenXrSource>();
    } else {
        app.source = std::make_unique<PimaxSource>();
    }

    printf("frame source: %s\n", app.source->Name());
    g_app = &app;

    FILE* trace = OpenTrace(app);
    FileSink traceSink;
    traceSink.file = trace;

    app.output.SetSize(app.settings.outputWidth, app.settings.outputHeight);

    const PipelineConfig config = ConfigOf(app.settings, app.source->Report());
    app.source->SetArrivalTiming(MakeArrivalTiming(config.arrivalTiming));

    std::string error;

    if (!app.source->Start(&error)) {
        printf("%s\n", error.c_str());
        MessageBoxA(nullptr, error.c_str(), "SimOptix: VR Mirror", MB_ICONERROR);
        return 1;
    }

    app.hwnd = CreateMirrorWindow(app.output.Width(), app.output.Height());

    if (!CreateDevice(app)) {
        printf("Could not create the D3D11 device/swap chain.\n");
        app.source->Stop();
        return 1;
    }

    app.output.ResizeSwapChain();
    SetImageNeed(app);
    app.source->OpenImages(app.output.Device());
    ShowWindow(app.hwnd, SW_SHOW);

    // Draw once per new mirror frame, right after it arrives: the game overwrites its single
    // buffer about one frame later, so copying late would pair a newer image with an older pose.
    FrameLoop loop;
    loop.lastReopen = app.source->Now();
    loop.lastLateReport = loop.lastReopen;

    // The pipeline for a window drawn once per game frame: its frame picker is NewestFramePicker
    // (Instant) or HeldFramePicker (Look-ahead). Frames are due the smoothing's delay plus the
    // present delay after their de-jittered arrival.
    BuildPipeline(config, &app.pipeline);
    app.feed.SetDrawnPose(app.pipeline.SharedDrawnPose());

    if (trace) {
        app.pipeline.SetTraceSink(&traceSink);
    }

    loop.lookahead = config.smoothing != SmoothingKind::kInstant;
    const double holdDelay = SmoothingDelayS(config.smoothing) + kPresentDelayMs / 1000.0;

    // Look-ahead smoothing: room for every frame inside the delay, and a few more.
    if (loop.lookahead) {
        app.hold.Resize(int(std::ceil(holdDelay / app.source->Period())) + 6);
    }

    app.pipeline.Picker()->Restart(holdDelay, loop.lookahead ? app.hold.Capacity() : 1);
    loop.stored.resize(size_t(app.hold.Capacity()) + 1);

    RunFrames(app, loop);

    if (trace) {
        app.pipeline.SetTraceSink(nullptr);  // its last row first
        fclose(trace);
    }

    app.source->Stop();
    g_app = nullptr;
    return 0;
}
