# e2e: the numbers to compare a run with

Terms: `reference\terms.md`. How to run the suite: `reference\e2e.md`. What each scenario checks:
`vr_mirror\e2e\README.md`.

Compare every e2e run with these numbers. When they change for a reason, update them here in the same
commit. Each section says its date and its conditions. Instant / Smooth / Smoothest is the order of
three numbers, unless a row says otherwise.

## The latest runs (2026-11-08)

Six runs of the whole suite. LMU, OBS and Pimax Play were closed for runs 1 to 4.

**Run 1: the build with the OpenXR source** (branch `claude/openxr-source-cf46ca`). All 54 scenarios
pass.
- The Pimax scenarios are within or beside the ranges of the baseline (below):
  - `stabilization`: 0.39 / 0.014 / 0.011 of the head's shake
  - `dropped_frames`: 0.47 / 0.025 / 0.024
  - `replay_queued_late_frames`: 7.1 / 5.0 / 8.1% from the wrong pose
  - the featureless replay: 1.50 / 0.32 / 0.22%
  - `game_starts_late`: 4.8 runtime calls/s idle, no first frame missed
- One change: the **recorded view jitter** is lower. `stabilization` 0.128 / 0.117 / 0.117°, and
  `dropped_frames` 0.176 / 0.160 / 0.161°. The baseline has 0.208 / 0.205 / 0.207° and 0.240 / 0.228 /
  0.227–0.230°.
  - `main`'s mirror app run with this runner and fake gives the same: 0.132 and 0.169° at Instant.
  - So the change comes from the test, not from the mirror app: the fake game's image (shared by both
    fakes since then), or Pimax Play closed.

**Run 2: the same code after the rename to VR Mirror,** with `stabilization` and the featureless replay
run three more times. It shows how far these two spread from run to run:
- `stabilization`'s shake at Instant: 0.39–0.48 of the head's (limit 0.5)
- the featureless replay at Instant: 1.6–1.9% from the wrong pose (limit 2.5%)

**Run 3: after the switch to the project's own PVR declarations**
(`vr_mirror\frame_sources\pimax\pvr_interface.h`). 53 of 54 scenarios pass.
- `stabilization` 0.40 / 0.014 / 0.011 of the head's shake, `dropped_frames` 0.44 / 0.022 / 0.026,
  `replay_queued_late_frames` 7.9 / 4.9 / 8.2% from the wrong pose, the featureless replay 1.50 / 0.32 /
  0.22%, and `game_starts_late` 4.8 runtime calls/s idle.
- `replay_late_frame_same_slot_smoothest` failed with 1 frame from the wrong pose (0.11%, 0.236°). It
  passed with none in three reruns of all three `late_frame_same_slot` scenarios.
- The mirror app reads the same values from the fake runtime as before, so the declarations are not a
  likely cause. The older build was not run again to compare.
- Compared with Instant, two of the four known OpenXR horizon comparisons failed (below):
  `openxr_game_behind` Smooth 0.278° against 0.207°, and `openxr_black_borders` Smoothest 0.777° against
  0.639°. The other 16 comparisons passed.

**Run 4: after the renames to the mirror app and the drawn pose** (on top of the code style changes).
The whole suite took 787 s. 53 of 54 scenarios pass.
- `stabilization` 0.39 / 0.013 / 0.010 of the head's shake, `dropped_frames` 0.50 / 0.024 / 0.032,
  `replay_queued_late_frames` 9.5 / 4.2 / 8.2% from the wrong pose, the featureless replay 1.61 / 0.21 /
  0.33%, and `game_starts_late` 4.8 runtime calls/s idle, no first frame missed.
- `queued_cockpit_smoothest` failed: the mirror app drew nothing for 7.8 s in the middle of the run,
  so 53.8% of game frames were missed. Other programs on the PC were busy at the time. A rerun of all
  three `queued_cockpit` scenarios passed, with no frame missed, a longest gap of 46 ms, visible pose
  jumps 1.10 / 1.38 / 1.34%, and the comparison with Instant passing.
- Compared with Instant, two of the four known OpenXR horizon comparisons failed (below):
  `openxr_stabilization` Smooth and Smoothest 0.279 and 0.290° against 0.226°, and
  `openxr_black_borders` Smoothest 0.777° against 0.703°. With the rerun, the other 16 passed.

**Runs 5 and 6: the view at the image's middle, with its camera levelled** (the fix for #5). LMU
was closed and OBS open. Whether Pimax Play ran was not checked.
- **Run 5, with the aim and before the camera was levelled:** 51 of 54 passed.
  - `openxr_black_borders` tilted 2.01 / 2.10 / 2.11° at its worst, which the levelling fixed
    (`findings\pipeline.md`, "Fixed: the same zoom showed a narrower view").
  - `openxr_stabilization` at Instant ran at 45 fps, skipping every other frame. It passed at 90 fps
    in the runs just before and after.
  - `black_borders_low_zoom` at Instant had one stall. It had none in run 6.
- **Run 6, the final build:** all 54 scenarios and all 18 comparisons with Instant pass, in 786 s.
  - `stabilization` 0.41 / 0.014 / 0.011 of the head's shake, `dropped_frames` 0.52 / 0.024 / 0.025,
    `replay_queued_late_frames` 7.8 / 4.2 / 7.8% from the wrong pose, the featureless replay 1.50 /
    0.32 / 0.33%, and `game_starts_late` 4.8 runtime calls/s idle.
  - Recorded view jitter: `stabilization` 0.130 / 0.116 / 0.118°, `dropped_frames` 0.184 / 0.159 /
    0.161°.
  - The view's width is 0.07–0.14° off what its zoom asks for in all 27 scenarios that read it
    (limit 0.2°).
  - **The four OpenXR horizon comparisons pass.** Worst tilt, Instant / Smooth / Smoothest:
    `openxr_stabilization` 0.191 / 0.173 / 0.183°, `openxr_dropped_frames` 0.240 / 0.159 / 0.160°,
    `openxr_game_behind` 0.150 / 0.158 / 0.168°, `openxr_black_borders` 0.183 / 0.170 / 0.164°. The
    Pimax scenarios: `stabilization` 0.125 / 0.103 / 0.106° (0.21–0.23° before).
  - `black_borders` still tilts 1.13–1.15° at its worst, at level pitch. It is 0.31° looking up or
    down.

**Run 7 (2026-11-09): the layer's image noted before xrEndFrame, with `openxr_stall` and the fake game
through the layer's frame code** (issue 12). LMU, OBS and Pimax Play were closed. The 57 runs took
837 s. 54 of 57 passed, and 17 of 19 comparisons with Instant.
- `openxr_stall_smoothest` failed: 5.9% from the wrong pose, 20.5° worst on screen. Another session's
  test started a fake OpenXR game during it, which wrote the same shared memory (the mirror app logged
  "another layout" and textures of 4168×27). Two e2e runs with OpenXR scenarios can't share the PC.
- `black_borders_low_zoom` at Instant had one stall (as in run 5), and `openxr_black_borders` Smooth
  tilted 0.245° against Instant's 0.128°.
- A rerun of those three, at all three smoothings, passed with their comparisons: `openxr_stall`
  0.053–0.054° rms on screen, worst 0.147–0.149°, `openxr_black_borders` tilts 0.216 / 0.172 / 0.164°.
- `stabilization` 0.41 / 0.014 / 0.011 of the head's shake, `dropped_frames` 0.51 / 0.038 / 0.025,
  `replay_queued_late_frames` 7.6 / 5.2 / 7.5% from the wrong pose, the featureless replay 1.61 /
  0.21 / 0.22%, and `game_starts_late` 4.8 runtime calls/s idle.

**The OpenXR scenarios** (the fake game with the layer's frames), from run 1:

| Scenario | Result |
|---|---|
| openxr_stabilization | pass: shake 0.34 / 0.013 / 0.013 of the head's, no frame from the wrong pose, no stall, lag up to 9.5 / 2.1 / 2.1°. On screen 0.05° image/pose mismatch (Pimax's stabilization: 0.51°) |
| openxr_dropped_frames | pass: shake 0.37 / 0.022 / 0.026 of the head's, no frame from the wrong pose, recorded view jitter 0.156 / 0.159 / 0.162° |
| openxr_game_behind | pass: no frame from the wrong pose, no stall, jitter 2.8 / 1.1 / 1.1 of the head's |
| openxr_black_borders | pass: 0% black, no stall, jitter 1.6 / 0.20 / 0.20 of the head's |
| openxr_game_starts_late | pass: the game's first frame missed at all three (the limit: the layer learns the image's size from it), none after, no frame from the wrong pose |
| openxr_stall | pass (2026-11-09, run 7's rerun): image/pose mismatch 0.053–0.054° rms at all three, worst frame 0.147–0.149°, no game frame missed, 88.6–88.7 fps. With the image looked up after xrEndFrame, as the layer did before: 0.73–0.75° rms, worst 2.8–4.5° |

**Compared with Instant, four fail in run 1, all on the horizon's worst tilt.** The limit: Smooth and
Smoothest at most 0.05° worse than Instant.

| Scenario | Worst tilt, Instant / Smooth / Smoothest |
|---|---|
| `openxr_stabilization` | 0.211 / 0.263 / 0.288° |
| `openxr_dropped_frames` | 0.198 / 0.314 / 0.221° |
| `openxr_game_behind` | 0.175 / 0.259 / 0.256° |
| `openxr_black_borders` | 0.642 / 0.648 / 0.777° |
| `stabilization` (Pimax), for comparison | 0.205 / 0.210 / 0.206° |

The 14 other comparisons pass, the Pimax ones among them. Not explained (`findings\pipeline.md`, "The
reported slot and pose"). The limit stays as it is.

## The baseline (2026-10-26)

A 320×180 window, LMU closed, Pimax Play open (see below), after the fix in `findings\e2e.md`,
"Smoothing compared with Instant: what it found".

| Scenario | Result |
|---|---|
| stabilization | pass: shake 0.39–0.43 / 0.014–0.026 / 0.012–0.018 of the head's, no stall, lag up to 5.2 / 3.9 / 3.9° |
| dropped_frames | pass: shake 0.49–0.59 / 0.021–0.022 / 0.018–0.019 of the head's, recorded view jitter 0.240 / 0.228 / 0.227–0.230° (stabilization: 0.208 / 0.205 / 0.207°) |
| head_still | pass: smoothed jitter 0.000° at all three (the head's 0.012°), horizon 0.04–0.05° |
| black_borders | pass: 0% black, no stall, jitter 2.0–5.2 / 0.19–0.23 / 0.19–0.27 of the head's, lag up to 8.1 / 9.7 / 9.7°, the edge limit never acting |
| black_borders_low_zoom | pass: 0% black, no stall. Jitter 3.1–3.5 / 0.85–1.05 / 0.40–0.48 of the head's, the edge limit on 1.6–1.7 / 0 / 0% of frames (Instant's catch-up, as before) |
| game_starts_late | pass: 4.8 runtime calls/s while no game ran, 0 first frames missed, 0% missed after, at all three |
| replay_false_retiming_head_turns | pass: 0–0.1% / 0% / 0–0.1% from the wrong pose |
| replay_false_retiming_short_hiccup | pass: 0% from the wrong pose at all three |
| replay_queued_late_frames | pass: 6.9–9.7% / 4.4–5.5% / 4.6–8.5% from the wrong pose (limits 12 / 7 / 10%, target 4%). Worst 0.69–0.74 / 0.38 / 0.38° |
| replay_false_retiming_featureless_cockpit | pass: 1.3–1.4% / 0.1–0.2% / 0.1–0.2% from the wrong pose (limits 2.5 / 1 / 1%). Worst 1.39 / 0.08–0.14 / 0.14° |
| replay_late_frame_same_slot | pass: no frame from the wrong pose at all three (2026-11-05, the build before fails with 1 frame, 0.17°) |

- **Compared with Instant, all pass** (`vr_mirror\e2e\README.md`, "Smoothing compared with Instant").
- **The replays' numbers** are from four runs on 2026-10-27, with Smooth and Smoothest frames judged in
  hindsight and decided at all (the flush fix in `findings\e2e.md`). LMU, OBS, Steam and Pimax Play
  closed.
- **The look-ahead's delay.** The look-ahead runs compare each frame's view with the head at the frame's
  own moment. The mirror app presents the frame its look-ahead delay later
  (`vr_mirror\core\pipeline\4_smoothing\lookahead_smoothing.h`, `LookaheadOutputDelayS`: 534 and
  1034 ms from arrival). So the runner takes the head at the presented time minus that delay (0 for
  Instant). The delay is steady to 2.3–2.5 ms, as Instant's 4 ms is.

**Two scenarios fail now and then,** on the build of 2026-11-05 and the one before alike. The cause is
not known.
- `replay_false_retiming_short_hiccup_smooth`: 3.5–4.0% of game frames missed (limit 1%) in 1 run of
  10 on the new build, and 1 of 9 on the old one. 0% in the others.
- `black_borders` (Instant): 1 stall mid-turn in 1 run of 4 on the new build (0 of 3 on the old one).
  Its jitter ratio varies a lot between runs at Instant: 2.1–4.7 on both builds.

**Pimax Play running during a run disturbs it,** because its compositor keeps rendering to the headset.
On 2026-10-26, with Pimax Play open, one run in five of `stabilization` (new and old builds alike) had
the mirror app's arrival timing noise at 1.46 ms rms instead of 0.52, 7.2% of game frames missed, and a
jitter ratio of 2.24. The reruns passed (0.42–0.47). Close Pimax Play for runs that are compared.

**The featureless replay varies by a frame or two from run to run** (993 frames, 13–16 from the wrong
pose). On 2026-10-26 four runs gave 1.40–1.58%. Three runs of the build before the other images were
retired gave 1.30–1.49%. The worst frame was 1.389° in all of them.

## Stage replay baseline (2026-10-30)

The configuration of the time (grid, image, fixed delay, predicted, planned, rotation), drawn as the
window at zoom 1.5, by smoothing. Lower is better in every column.

| Input | Smoothing | black | lag p99 / max | jitter still / small | edge limit | planned | wrong pose |
|---|---|---|---|---|---|---|---|
| queued_late_frames | Instant / Smooth / Smoothest | 0 / 0 / 0% | 4.51 / 0.89 / 0.72°, max 4.59 / 1.12 / 0.92° | 0.0002 / 0.0010 / 0.0005°, 0.0003 / 0.0011 / 0.0006° | 0% | 0% | 0% |
| false_retiming_head_turns | | 0% | 3.35 / 0.98 / 0.86°, max 3.54 / 1.13 / 1.01° | 0.0001 / 0.0010 / 0.0007°, 0.0002 / 0.0015 / 0.0008° | 0% | 0% | 0% |
| false_retiming_short_hiccup | | 0% | 4.25 / 1.25 / 1.04°, max 4.63 / 1.72 / 1.62° | 0.0002 / 0.0012 / 0.0008°, 0.0003 / 0.0017 / 0.0009° | 0% | 0% | 0% |
| false_retiming_featureless_cockpit | | 0% | 5.04 / 1.28 / 1.21°, max 5.48 / 1.41 / 1.37° | 0.0003 / 0.0012 / 0.0008°, 0.0007 / 0.0013 / 0.0008° | 0% | 0% | 0% |
| edge_test+heartbeat | | 0% | 11.07 / 4.85 / 4.90°, max 11.79 / 5.03 / 5.08° | 0.0051 / 0.0190 / 0.0190°, 0.0208 / 0.0859 / 0.0860° | 0% | 0% | 0% |
| racing+heartbeat | | 0% | 7.10 / 1.68 / 1.68°, max 7.49 / 2.46 / 2.46° | 0.0011 / 0.0012 / 0.0006°, 0.0072 / 0.0342 / 0.0342° | 0% | 0% | 0% |
| edge_test+heartbeat, zoom 1.2 | | 0% | 8.95 / 4.49 / 4.63°, max 9.37 / 4.68 / 5.03° | 0.0118 / 0.0192 / 0.0190°, 0.0551 / 0.0899 / 0.0860° | 1.21 / 0 / 0% | 0 / 23.84 / 22.94% | 0% |

- `--drawn_pose arrival_slot` (was `--frame_matching none`) on `queued_late_frames`: 9.32% from the
  wrong pose. e2e without the matching: 9.7%.
- `--drawn_pose measured`: 98.15%, because the measured head is never exactly the pose a frame was
  drawn with. The measured drawn pose was removed on 2026-11-07.
- Instant's dead zone holds a slow view still, so it reads lowest while still, as in e2e.
- **Not explained:** on the synthetic motions the look-ahead reads higher than Instant in small moves
  (`racing+heartbeat` 0.034° against 0.007°), where e2e's shake measure, on the presented frames, finds
  it far calmer. The two measures differ in what they see: here the intended view per frame, with exact
  predictions and no presentation. Which part of the look-ahead's path the fit leaves was not looked
  into.

## Proven to fail (2026-10-25, the last rows 2026-11-08)

Each test, run with what it checks broken, against the build of the time. "Broken" is a setting, a fake
that draws less, or the build that had the bug, made from its commit and run with `--tool`.
- Run at 320×180.
- The settings `stabilize` and `roll_lock` were removed on 2026-11-07. `--set smoothing=0` switches
  stabilization off, and the horizon is always level.
- "The tool" is the mirror app's old name.

| Test | Broken how | Result broken | Result now |
|---|---|---|---|
| black_borders | the game image 20% smaller than the tool assumes (`--fake drawn_fov=0.8`) | 16.8% of frames with black (before the outermost-line check) | 0% |
| black_borders | 10% smaller (`drawn_fov=0.9`) | 0%: the view never gets that far out | 0% |
| black border check | view held at the image's edge (`--set zoom=1.0`), a black band drawn inside it | 1.6 px band (`drawn_fov=0.99`): 99.7%. 0.8 px (`0.995`): 0% | – |
| stalls mid-turn | no setting found that stalls (catch-up off, with and without a smaller lag limit) | not shown | 0 |
| stabilization | `--set stabilize=0` | vibration 3.96 of the head's (limit 0.5): fails | 0.40–0.44 |
| head_still | `--set stabilize=0` | smoothed jitter 0.012° (limit 0.005°), horizon 0.70° (limit 0.2°): fails | 0.000°, 0.04° |
| head_still | `--set roll_lock=0` | horizon 0.59°: fails | 0.04° |
| game_starts_late | the build before the idle back-off | 2,815 runtime calls/s while no game ran (limit 50): fails | 4.8/s |
| black_borders_low_zoom_smoothest | the build before the edge planning | jitter 1.06 of the head's (limit 0.7), the edge limit on 1.6% of frames (limit 0.5%): fails | 0.40–0.48, 0% |
| black_borders_low_zoom_smooth | the same | the edge limit on 1.57% of frames (limit 0.5%): fails | 0% |
| stabilization_smoothest, _smooth | Instant smoothing on the same motion (stabilization) | jitter 0.39–0.44 of the head's (limit 0.1): fails | 0.012–0.026 |
| black_borders_smoothest | Instant on the same motion (black_borders) | jitter 1.8–2.8 of the head's (limit 1.0): fails | 0.19–0.27 |
| comparison: featureless_cockpit | the view for the slot a late frame was drawn for (before `ShownTime`, 2026-10-26) | smoothed jitter 0.031 / 0.032° against Instant's 0.002° (allowed 0.007°): fails | 0.002 / 0.001° |
| replay_false_retiming_head_turns_smoothest | the pose predictions kept for 0.7 s instead of 2.8 s (a frame's prediction gone when it is shown) | 90.8% from the wrong pose | 0–0.1% |
| queued_cockpit | the cockpit correction without its pull back to the head poses (time constant 1000 s) | correction drift 1.43° (limit 0.5°): fails. Visible jumps 0.6%. With the pose drift that replaced it (2026-10-31): 2.34°: fails. Visible jumps 0.5% | 0.09°. Pose drift 0.29–0.31° |
| replay_queued_late_frames | no frame matching (the build just before it) | 9.7% from the wrong pose (then limit 4%), worst 0.69° | 6.9–9.7% at Instant: within its 12% now, so not caught |
| replay_queued_late_frames_smoothest | held frames' thumbnails not submitted (the build before that was fixed) | 12.2–12.4% from the wrong pose (limit 10%), 22% of frames undecided | 4.6–8.5% |
| replay_false_retiming_head_turns | the first build with frame matching | 9.1% (limit 1%), worst 0.33° | 0.1% |
| replay_false_retiming_short_hiccup | the first build with frame matching | 26.6%, worst 0.59° | 0% |
| replay_false_retiming_short_hiccup | the build whose gate opened on two drops | 7.4%, worst 0.37° | 0% |
| replay_false_retiming_featureless_cockpit | the build before featureless texels were left out | 3.4% (Instant limit 2.5%), worst 1.39° | 1.3–1.4% |
| replay_late_frame_same_slot, _smooth, _smoothest | the arrival timing rounding to the nearest slot (the build of 2026-11-05) | 1 frame from the wrong pose (0.10–0.11%, limit 0), the late frame, 0.170° off: fails at all three | 0 frames, 0.000° |
| stabilization (view width, 2026-11-08) | the build before the view looked at the image's middle (`main` at e58e8fe, zoom 1.5) | width 42.64° against 43.07° asked: 0.44° off (limit 0.2°): fails | 0.08° off |
| openxr_stabilization (view width and direction, 2026-11-08) | the same build | width 40.04° against 43.47° asked: 3.43° off: fails. Image/pose mismatch 2.02° rms (limit 1.5°): fails | 0.08° off, 0.05° rms |
| black_borders (tilt looking up or down, 2026-11-08) | the same build | 1.21° (limit 0.5°): fails | 0.31° |
| openxr_black_borders (the same) | the same build | 0.64°: fails. With the aim at the image's middle and the camera not levelled: 2.01° (Smooth 2.10°) | 0.11° |

`head_still` first passed broken too. Its head's tremor was all at 5–8 Hz, which the jitter measure
barely sees: a quadratic fit over ±50 ms takes up 90% of 8 Hz and more below, 61% of 12 Hz and 29% of
15 Hz. So it measured 0.003°, and its head tilted under 0.1°. Its motion has had a fine shake and a lean
since (`vr_mirror\e2e\README.md`, "Scenarios"), with the limits unchanged.

## Window size (2026-10-25)

Every scenario runs the mirror app in a 320×180 window, its smallest (`--output` overrides). What is
read from the window reads the same as at larger sizes:

| | 640×360 | 480×270 | 320×180 |
|---|---|---|---|
| stabilization: vibration, of the head's | 0.44 | 0.43 | 0.40 |
| head_still: horizon tilt | 0.06° | 0.05° | 0.04° |
| decodable captures | 100% | 100% | 100% |

- Replays don't depend on the window at all. The slot check works on 64×32 thumbnails of the eye
  image, and replays are judged by poses.
- Black borders are caught by pixels, so their size in degrees grows as the window shrinks.
  - With each patch judged by its outermost line, a band of 1.6 px is caught at 320×180 (99.7% of
    frames), and 0.8 px is missed. That is about 13 px on a 2560-wide stream.
  - Judged by 8% of a whole patch, it took between 3.2 and 4.8 px (30–40 px on the stream).
