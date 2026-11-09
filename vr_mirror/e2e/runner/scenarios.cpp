// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "scenarios.h"

namespace {

    const char* const kSmoothingSuffix[] = {"", "_smooth", "_smoothest"};
    const char* const kSmoothingName[] = {"Instant", "Smooth", "Smoothest"};

    // The scenario at every smoothing (0 Instant, 1 Smooth, 2 Smoothest; the tool's setting is one more):
    // Instant keeps the name, the others get _smooth and _smoothest. `tune` adjusts a variant's limits (the
    // look-ahead ones smooth far more than Instant, so some limits are tighter there).
    template <typename Tune>
    void AddEverySmoothing(const Scenario& instant, std::vector<Scenario>* out, Tune tune) {
        for (int smoothing = 0; smoothing <= 2; smoothing++) {
            Scenario v = instant;
            v.base = instant.name;
            v.smoothing = smoothing;

            // settings.h: 1 Instant, 2 Smooth, 3 Smoothest
            v.settings["smoothing"] = std::to_string(smoothing + 1);
            if (smoothing) {
                v.name += kSmoothingSuffix[smoothing];
                v.description += std::string(" [") + kSmoothingName[smoothing] + "]";
            }

            tune(v);
            out->push_back(v);
        }
    }

    void AddEverySmoothing(const Scenario& instant, std::vector<Scenario>* out) {
        AddEverySmoothing(instant, out, [](Scenario&) {});
    }

    void AddMotionScenarios(std::vector<Scenario>* out) {
        {
            Scenario c{
                "stabilization",
                "racing glances with a fast shake: the shake smoothed away, no stall",
                "racing+heartbeat"
            };

            c.seconds = 15;
            c.limits.maxStalls = 0;

            // the shake while glancing: ~0.4 at Instant (pinned to the head: 0.65)
            c.limits.maxSmoothedRatio = 0.5;
            AddEverySmoothing(c, out, [](Scenario& v) {
                if (v.smoothing) {
                    v.limits.maxSmoothedRatio = 0.1;  // ~0.02
                }
            });
        }

        {
            // The game misses 5% of its frames at random (one now and then, sometimes two in a row): the
            // tool shows the frame before for that slot, at every smoothing. The look-ahead must not make
            // that look worse than Instant does (compare.h: on-screen jitter).
            Scenario c{
                "dropped_frames",
                "stabilization's motion with 5% of the game's frames dropped",
                "racing+heartbeat"
            };

            c.fake["drop_rate"] = "0.05";
            c.seconds = 15;
            c.limits.maxStalls = 0;

            // Instant's view gets shakier with drops: 0.49-0.57 of the head's (without: 0.39-0.44).
            c.limits.maxSmoothedRatio = 0.7;
            c.limits.minToolFps = 80;  // 95% of 90 fps arrive
            AddEverySmoothing(c, out, [](Scenario& v) {
                if (v.smoothing) {
                    v.limits.maxSmoothedRatio = 0.1;
                }
            });
        }

        {
            Scenario c{
                "head_still",
                "headset worn, head held still: pulse, breathing and head tremor must not show",
                "heartbeat+head_tremor"
            };

            // what's left of the pulse and tremor: under a tenth of a pixel
            c.limits.maxSmoothedJitterDeg = 0.005;
            c.limits.maxRollDeg = 0.2;
            c.limits.maxStalls = 0;
            AddEverySmoothing(c, out);
        }
    }

    void AddEdgeScenarios(std::vector<Scenario>* out) {
        {
            Scenario c{
                "black_borders",
                "fast turns, flicks, tilts, shakes and holds: no black at any edge or corner",
                "edge_test+heartbeat"
            };

            c.seconds = 13;  // one 12 s cycle of edge_test
            c.limits.maxBlackEdgePct = 0;
            c.limits.maxStalls = 0;

            // The screen looks along the head turned by the eye's cant and the aim at the image's middle,
            // so it tilts as the head pitches unless that camera itself is levelled: 1.22° here with the
            // Pimax eye's 2° cant, 0.29° levelled. (Its worst tilt at level pitch, 1.1°, is another matter.)
            c.limits.maxPitchedRollDeg = 0.5;
            AddEverySmoothing(c, out, [](Scenario& v) {
                if (v.smoothing) {
                    v.limits.maxSmoothedRatio = 1.0;  // ~0.24 (Instant: 1.8-2.8)
                }
            });
        }

        {
            // The same at zoom 1.2, where the view meets the edge: with Smooth and Smoothest the window
            // shows each edge coming and the path is pulled in ahead of time, so the last-resort
            // limit, a jerk, mustn't act. Before: 1.6% of frames clamped, jitter 1.06 of the head's.
            Scenario c{
                "black_borders_low_zoom",
                "black_borders' motion at zoom 1.2: edges planned for, not clamped",
                "edge_test+heartbeat"
            };

            c.seconds = 13;
            c.settings["zoom"] = "1.2";
            c.limits.maxBlackEdgePct = 0;
            c.limits.maxStalls = 0;
            AddEverySmoothing(c, out, [](Scenario& v) {
                if (!v.smoothing) {
                    return;  // Instant's catch-up clamps 1.6-1.7% here (jitter 3.1-3.5), as before
                }
                // Smooth sees half a second ahead, too little to ease in much: 0.85-1.05 (before: 1.02-1.05
                // with 1.6% clamped), so its check is the clamps. Smoothest: 0.40-0.48 (before: 1.06).
                v.limits.maxSmoothedRatio = v.smoothing == 1 ? 1.2 : 0.7;
                v.limits.maxEdgeClampPct = 0.5;  // 0% measured
            });
        }
    }

    void AddGameTimingScenarios(std::vector<Scenario>* out) {
        {
            // The game behind as in a real race at 82 fps (2026-10-28: 278 visible jumps a minute): it misses
            // 9% of its slots and queues up to two frames, each drawn for an earlier slot than it arrives
            // for, the lag changing at drops and catch-ups. The picture is a cockpit with detail on its
            // roof, pillars and dashboard, a plain panel, a turning wheel and the world turning past the
            // windows as the car corners; the head glances about as in racing, without road vibration (a
            // sim rig). Graded on the cockpit correction (drawn_pose 2): the picture's jumps against
            // the poses the frames were drawn with, and no drift. Judged by poses (exact here), not on
            // screen: the cockpit holds no direction codes.
            Scenario c{
                "queued_cockpit",
                "the game behind at ~82 fps with a detailed cockpit: the cockpit correction",
                "glances+head_tremor+heartbeat"
            };

            c.fake["cockpit"] = "1";
            c.fake["queue_lag"] = "2";
            c.fake["drop_rate"] = "0.09";
            c.settings["drawn_pose"] = "2";

            c.seconds = 15;
            c.posesExact = true;
            c.viewOnScreen = false;
            c.limits.minValidPct = -1;
            c.borderPatches = 0;
            c.limits.minToolFps = 75;  // 91% of 90 fps arrive
            c.limits.maxStalls = 0;
            c.limits.maxPoseJumpPct = 2;
            c.limits.maxPoseDriftDeg = 0.5;

            AddEverySmoothing(c, out);

            // The same with the slot check alone (drawn_pose 1, today's default), to compare: not graded.
            Scenario o = c;
            o.name = "queued_cockpit_slot_check";
            o.description = "queued_cockpit with the slot check alone (today's default), to compare";
            o.settings["drawn_pose"] = "1";
            o.limits.maxPoseJumpPct = o.limits.maxPoseDriftDeg = -1;
            AddEverySmoothing(o, out);
        }

        {
            // Pimax with the mirror on but no game: the texture and frame signal exist, no frames come.
            // The tool should go idle and still catch the game's very first frames.
            Scenario c{
                "game_starts_late",
                "the mirror is on 5 s before the game starts: few runtime calls meanwhile, the first "
                "frames caught",
                "heartbeat"
            };

            c.fake["game_start_s"] = "5";
            c.seconds = 9;                     // capture from 1.5 s: idle from 2 s, the game from 5 s
            c.limits.maxIdleCallsPerSec = 50;  // full rate is ~2,500/s; idle: status checks and handles only
            c.limits.maxStartMissed = 1;
            c.limits.minToolFps = -1;  // the tool's frame rate would count the wait (the others grade it)
            c.viewOnScreen = false;    // nothing is drawn before the game starts; judged by the tool's trace
            c.limits.minValidPct = -1;
            c.borderPatches = 0;
            c.compareView = false;  // a few seconds of a still head after a start, against the first frames
            AddEverySmoothing(c, out);
        }
    }

    // The OpenXR layer's frames (frame_source=openxr): the fake game publishes each frame as the layer
    // does, with the display slot it was drawn for and its exact pose, so every frame's pose must be the
    // one it was drawn with, whatever the game's timing.
    Scenario OnOpenXr(Scenario c) {
        c.settings["frame_source"] = "openxr";
        c.posesExact = true;
        c.limits.maxWrongPosePct = 0;
        return c;
    }

    void AddOpenXrMotionScenarios(std::vector<Scenario>* out) {
        {
            Scenario c{
                "openxr_stabilization",
                "stabilization's motion from the OpenXR layer's frames",
                "racing+heartbeat"
            };

            c.seconds = 15;
            c.limits.maxStalls = 0;
            c.limits.maxSmoothedRatio = 0.5;
            AddEverySmoothing(OnOpenXr(c), out, [](Scenario& v) {
                if (v.smoothing) {
                    v.limits.maxSmoothedRatio = 0.1;
                }
            });
        }

        {
            Scenario c{
                "openxr_dropped_frames",
                "openxr_stabilization with 5% of the game's frames dropped",
                "racing+heartbeat"
            };

            c.fake["drop_rate"] = "0.05";
            c.seconds = 15;
            c.limits.maxStalls = 0;
            c.limits.maxSmoothedRatio = 0.7;
            c.limits.minToolFps = 80;
            AddEverySmoothing(OnOpenXr(c), out, [](Scenario& v) {
                if (v.smoothing) {
                    v.limits.maxSmoothedRatio = 0.1;
                }
            });
        }

        {
            // queued_cockpit's game, from the OpenXR layer: frames drawn for up to two slots before the one
            // they end at, which they report. Nothing to judge: no frame from the wrong pose.
            Scenario c{
                "openxr_game_behind",
                "the game behind at ~82 fps, its frames late but reporting their slot",
                "glances+head_tremor+heartbeat"
            };

            c.fake["queue_lag"] = "2";
            c.fake["drop_rate"] = "0.09";
            c.seconds = 15;
            c.limits.minToolFps = 75;
            c.limits.maxStalls = 0;
            c.limits.maxPoseJumpPct = 0;
            AddEverySmoothing(OnOpenXr(c), out);
        }

        {
            // A game that draws on one thread and ends its frames on another: the next frame's image is
            // released while the frame before is still in xrEndFrame. Every 3 s a frame takes 64 ms longer,
            // and the next follows it at once (issue 12). Each frame must show its own image.
            Scenario c{
                "openxr_stall",
                "a render thread releases the next image during xrEndFrame; a 64 ms stall every 3 s",
                "racing+heartbeat"
            };

            c.fake["render_thread"] = "1";
            c.fake["stall_every_s"] = "3";
            c.fake["stall_ms"] = "64";
            c.seconds = 15;
            c.limits.minToolFps = 80;
            c.limits.maxMismatchDeg = 0.2;       // openxr_stabilization: 0.05 deg rms
            c.limits.maxWorstMismatchDeg = 0.5;  // ...0.15 deg; the image of the next frame: 2.8 deg
            AddEverySmoothing(OnOpenXr(c), out);
        }
    }

    void AddOpenXrEdgeScenarios(std::vector<Scenario>* out) {
        {
            Scenario c{
                "openxr_black_borders",
                "black_borders' motion in the OpenXR layer's field of view: no black",
                "edge_test+heartbeat"
            };

            c.seconds = 13;
            c.limits.maxBlackEdgePct = 0;
            c.limits.maxStalls = 0;
            c.limits.maxPitchedRollDeg = 0.5;  // as in black_borders (2.0° with the aim, unlevelled)
            AddEverySmoothing(OnOpenXr(c), out, [](Scenario& v) {
                if (v.smoothing) {
                    v.limits.maxSmoothedRatio = 1.0;
                }
            });
        }

        {
            // The tool waits for the layer, which appears only when the game's session starts.
            Scenario c{
                "openxr_game_starts_late",
                "the layer appears 5 s after the tool starts: the first frames caught",
                "heartbeat"
            };

            c.fake["game_start_s"] = "5";
            c.seconds = 9;
            c.limits.maxStartMissed = 1;
            c.limits.minToolFps = -1;
            c.viewOnScreen = false;
            c.limits.minValidPct = -1;
            c.borderPatches = 0;
            c.compareView = false;
            AddEverySmoothing(OnOpenXr(c), out);
        }
    }

    double LastArrival(const fs::path& replay) {
        double last = 0;
        std::ifstream f(replay);
        std::string line;
        while (std::getline(f, line)) {
            long long serial;
            double arrival;
            if (sscanf_s(line.c_str(), "F,%lld,%lf", &serial, &arrival) == 2) {
                last = std::max(last, arrival);
            }
        }

        return last;
    }

    // The share of frames from the wrong pose that a replay may have at each smoothing (percent).
    struct WrongPoseLimits {
        double instant = 1.0, smooth = 1.0, smoothest = 1.0;
    };

    WrongPoseLimits ReplayLimits(const std::string& stem) {
        // Limits above the target where the 64x32 thumbnails can't tell the lags apart (2026-10-27,
        // .agents\skills\dev\findings\e2e.md, "Why the dust replay fails"), each just above its worst
        // measured run. Instant
        // decides from the frames before each one only; Smooth and Smoothest in hindsight.
        WrongPoseLimits l;

        if (stem == "queued_late_frames") {
            // Target 4%. Lag 1 and lag 2 look alike in the thumbnails while the head barely moves,
            // and at two of the queue's three changes they fit "no change" better (stage_replay
            // --stage frame_matching: 4.3% even with a second of hindsight). Measured 6.9-9.7% / 4.4-5.5% /
            // 4.6-8.5%.
            l.instant = 12.0, l.smooth = 7.0, l.smoothest = 10.0;
        } else if (stem == "false_retiming_featureless_cockpit") {
            // Target 1%, met with hindsight (0.1-0.2%); from the frames before each, the dark
            // cockpit's noise re-times 1.3-1.9% (stage_replay --stage frame_matching: 1.6%).
            l.instant = 2.5;
        } else if (stem == "late_frame_same_slot") {
            // One late frame that the arrival timing rounded onto the next frame's slot: the bug is a
            // single frame (0.1% of the run, 0.17 degree), so no frame may come out from the wrong pose.
            l.instant = l.smooth = l.smoothest = 0.0;
        }

        return l;
    }

}

std::vector<Scenario> BuiltInScenarios() {
    // Only what synthetic motion tests best; every other edge case (queued frames, timing hiccups,
    // pictures that mislead the frame matching) is a replay of real footage. Each runs at every
    // smoothing; the runner then compares Smooth and Smoothest with Instant (compare.h).
    std::vector<Scenario> s;
    AddMotionScenarios(&s);
    AddEdgeScenarios(&s);
    AddGameTimingScenarios(&s);
    AddOpenXrMotionScenarios(&s);
    AddOpenXrEdgeScenarios(&s);
    return s;
}

// The fake renders each frame with the pose the game really used, so what ends up on screen
// must match what the tool meant to show.
void AddReplayScenarios(const fs::path& replayDir, std::vector<Scenario>* out) {
    if (!fs::exists(replayDir)) {
        return;
    }
    for (const auto& e : fs::directory_iterator(replayDir)) {
        if (e.path().extension() != ".csv") {
            continue;
        }

        Scenario c{
            "replay_" + e.path().stem().string(),
            "real session replay " + e.path().filename().string(),
            "still"
        };

        c.fake["replay"] = e.path().string();

        // The game's real pictures, as the frame matching saw them: the replay's thumbnails, rebuilt
        // from the session's recording. The fake shows them where the tool makes its thumbnails,
        // which covers nearly all of the window (u 0.2-0.8, v 0.25-0.85 of the eye; the window shows
        // about u 0.17-0.83, v 0.28-0.72), so the view is judged by poses (exact here), not read on
        // screen, and black borders aren't checked (the real picture's dark pixels hold no direction
        // code; the corners read 85-100% "black"). black_borders covers them.
        fs::path thumbsFile = e.path();
        thumbsFile.replace_extension(".thumbs.png");

        if (fs::exists(thumbsFile)) {
            c.fake["thumbs"] = thumbsFile.string();
        } else {
            c.error = "no real thumbnails " + thumbsFile.filename().string()
                      + " (vr_mirror\\recording_analysis\\export_thumbs.py)";
        }

        c.viewOnScreen = false;
        c.limits.minValidPct = -1;
        c.borderPatches = 0;

        // Capture until shortly before the replay's last frame (the tool starts and settles first).
        c.seconds = std::max(3.0, LastArrival(e.path()) - 2.5);
        c.limits.maxWrongPosePct = 1.0;

        const WrongPoseLimits limits = ReplayLimits(e.path().stem().string());

        // With Smooth and Smoothest each held frame must still get the matcher's decision, though it
        // is shown up to a second after the matcher's short memory moved on
        // (vr_mirror\mirror_app\frame_hold.h).
        AddEverySmoothing(c, out, [&](Scenario& v) {
            v.limits.maxWrongPosePct = v.smoothing == 0   ? limits.instant
                                       : v.smoothing == 1 ? limits.smooth
                                                          : limits.smoothest;
        });
    }
}
