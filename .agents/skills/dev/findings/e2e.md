# Findings: the e2e suite and what it found

Terms: `reference\terms.md`. How to run the suite and `stage_replay`: `reference\e2e.md`. The numbers
to compare a run with: `reference\e2e_results.md`. What each scenario checks: `vr_mirror\e2e\README.md`.

Reading notes:
- Each section is dated. "Today" and "now" in a section mean as of its date.
- **The tool** is the mirror app's old name. **The frame matching** and **the matcher** are the slot
  check's old names, from before it was one implementation of the drawn pose stage.
- **`lag_sim`** is the old name of `stage_replay --stage drawn_pose`.
- Lower is better in every measure unless a row says otherwise.

## One prediction per slot (the fake runtime)

**What was wrong.** Before this change the fake runtime extrapolated a slot's predicted pose afresh each
time it was asked.
- The fake game asked when the slot was announced, and drew with that pose.
- The mirror app's poll thread asked a moment later, and recorded a different pose.
- So in fast moves the mirror app warped each frame from a pose the game never drew with. In
  `black_borders`' fast shake the difference reached up to the 1.7° rms that the earlier one-pipeline
  attempt measured.

A real runtime predicts a slot once, and the replays show the same pose to every asker. Since this
date the fake extrapolates a slot's pose from the moment the slot became the next one, whoever asks.
Replays are unchanged, because they play recorded predictions.

**How the baseline moved** (the mirror app of the day, 320×180, one full run each, plus two more runs
of `black_borders_low_zoom`):

| Scenario | Measure | Before | After |
|---|---|---|---|
| black_borders_low_zoom | jitter ratio smoothed/head, Instant / Smooth / Smoothest | 2.9–4.9 / 0.90–1.12 / 0.42–0.48 | 2.0–4.0 / 0.92 / 0.39 (all three runs alike) |
| black_borders | jitter ratio, Instant | 1.78 | 3.53 (within Instant's spread, 2.0–5.2 in the baseline's runs) |
| stabilization, dropped_frames | image/pose mismatch | 0.48–0.54° | 0.53–0.59° |
| replays | frames from the wrong pose | | within one frame of before |
| game_starts_late, Smoothest | game frames missed | 0% | 0.23% (one run, 3 frames skipped once, passes, not looked into) |

- All 30 runs and the 10 comparisons with Instant pass after it, as before.
- The look-ahead's low-zoom jitter now repeats run to run (0.39 three times at Smoothest). It spread
  by ±10% before.
- The mismatch rose slightly, because Instant follows the image's pose, which is now the prediction
  made 23 ms ahead rather than the mirror app's later, closer one. It is an informational measure
  (limit 1.5°).

## Drawing on ticks

An earlier attempt at one pipeline for both hosts (branch `os6-frame-pipeline`, not merged) made the
window draw on fixed ticks like the OBS plugin, and changed three more things at once. Two results got
worse: `black_borders_low_zoom`'s jitter at Smooth and Smoothest, and 0.2–1.0% of game frames missed.
Its pieces were made separate options and measured one at a time, each variant as a full run plus two
more runs of `black_borders_low_zoom`. The options have since been removed
(`findings\pipeline.md`, "Removed: the measured drawn pose and the slot-timeline frame picker").

| Run | How the window draws | Frame picker | Edges |
|---|---|---|---|
| A | once per game frame (today) | today's | planned |
| B | ticks (`output_clock=ticks`) | fixed delay (the plugin's) | planned |
| C | ticks | slot timeline | planned |
| D | ticks | fixed delay | against the shown image |
| E | ticks | slot timeline | against the shown image (the earlier attempt as a whole) |

After the fake runtime's fix ("One prediction per slot"), ranges over the runs:

| Measure | A | B | C | D | E |
|---|---|---|---|---|---|
| low-zoom jitter/head, Instant | 2.04–4.01 | 3.36–3.39 | 1.77–1.83 | 2.64–2.68 | 2.27–2.30 |
| low-zoom jitter/head, Smooth (limit 1.2) | 0.92 | 1.52 | 1.07–1.08 | 0.51–0.55 | **1.89–1.98** |
| low-zoom jitter/head, Smoothest (limit 0.7) | 0.39 | **1.58–1.59** | **0.98** | 0.48–0.49 | 0.46 |
| low-zoom edge limit %, Instant | 1.52–1.59 | 20.5 | 1.45–1.52 | 27.1–27.5 | 0–0.08 |
| low-zoom edge limit %, Smooth (limit 0.5) | 0 | 0 | 0 | **0–1.19** | 0.08 |
| low-zoom edge limit %, Smoothest (limit 0.5) | 0 | 0 | 0 | **0–0.90** | **0.66–0.74** |
| black_borders (zoom 1.5) edge limit %, Instant | 0 | 0.15 | 0 | 6.57 | 0 |
| stabilization shake/head, Instant | 0.39 | 0.17 | 0.31 | 0.34 | 0.31 |
| dropped_frames recorded jitter, Instant | 0.182° | 0.122° | 0.124° | 0.128° | 0.125° |
| game frames missed, synthetic (Instant, Smooth) | 0% | 0.21–0.28% | 0.21–0.28% | 0.21–0.28% | 0.21–0.28% |
| game frames missed, replays (worst run) | 0% | 0.89% | 0.77% | 0.81% | 0.70% |
| queued replay: ticks repeating / skipping one, Instant | 0 / 0% | 2.7 / 0.65% | 2.5 / 0.54% | 2.6 / 0.43% | 2.6 / 0.43% |
| queued replay: wrong pose, Instant / Smooth / Smoothest | 9.3 / 5.6 / 8.3% | 8.7 / 5.5 / 8.0% | 8.9 / 5.4 / 4.4% | 7.9 / 5.3 / 4.6% | 7.3 / 5.2 / 4.6% |
| black borders, any scenario | 0% | 0% | 0% | 0% | 0% |
| failed | none | low zoom Smooth, Smoothest | low zoom Smoothest | low zoom Smooth, Smoothest | low zoom Smooth, Smoothest |

Before the fake's fix the same pattern held, less steadily:
- E's Smooth jitter spread 0.64–4.49 from run to run (the earlier attempt's "unstable" 0.68–2.7), and
  its Smoothest limit 0.66–0.82%.
- D read 0.49–0.80 at Smoothest, and A 0.42–0.48.
- One E run of `head_still` had a 377 ms gap in the mirror app's output (3.2% missed). Two reruns had
  none: the PC was busy at the time.

### Which piece causes what

- **The jitter at low zoom comes from drawing on ticks with the edges of the time** (B), and depends
  on the frame picker.
  - In the mirror app's trace, the shown image's drawn slot runs **16.2 ms ahead of the tick's moment**
    with the fixed delay, and **−5.6 ms** (half a period) with the slot timeline. The moment is "tick
    minus the output delay", on the frames' arrival timeline, about 22 ms before their slots.
  - The planned edges of the time planned each path moment against the image at the moment's own slot.
    On ticks the moments fall between slots, so the lookup found no prediction and used the measured
    head.
  - They kept the view inside the shown image only with a **second, uncounted clamp**, a hard clip. On
    ticks that clip acted in every fast turn at zoom 1.2, and the edge-limit count never showed it.
    B's view had a residual of 0.02–0.04° in seconds where A has 0.005–0.010°, and 0.138° in the fast
    shake against A's 0.044°.
  - Shown by switching the clip off. Offline (`stage_replay --ticks 90 --zoom 1.2`), the fixed delay's
    Smoothest jitter in small moves went 0.058 → 0.006°, with **15% of outputs black**. In e2e, C
    without it went 1.07 → 0.54 (Smooth) and 0.98 → 0.20 (Smoothest), with **0.98% black**.
  - The slot timeline (C) halves the jitter, by bringing the moments onto the images. What is left is
    the fast shake. There the shown image is the runtime's prediction, which overshoots on reversals,
    while the planning checked the measured head.
- **Edges against the shown image** (D, E) remove the hidden clip, but bring their own problems.
  - With the fixed delay (D), the view trails the shown image by 16 ms of head motion. So the planning
    acts on nearly every tick (90 per second through seconds 1–9), and the counted limit acts now and
    then (0–1.19%, 27% at Instant).
  - With the slot timeline (E), at Smoothest **7 of the 8 limit actions fall on the ticks right after
    the copy delay is fixed** (1.03–1.09 s after the first tick). The picker jumps 3 frames ahead at
    once, the slot timeline eases the moments over 0.3 s, and the edges' averaged lead (about 1 s)
    lags behind. So the shown image is about 36 ms further ahead than the planning assumed. This is
    the earlier attempt's "planning and clamp disagreed at the first big turn", about 2 s after the
    game starts.
  - At Smooth, the jitter is the fast reversal at 7.4–7.5 s. Planning against the overshooting
    predictions pulls the path in irregularly: view steps of 1.1–5.5° per tick, where C's rise smoothly
    from 1.3 to 3.6°, and the gap from image to view reaching 5.0° against C's 4.4°.
- **The missed frames come from the ticks themselves,** the same with either picker or edges, and not
  from the fake's fix.
  - Each run skips **3 frames once, on the tick the copy delay is fixed**. The picker assumes 40 ms for
    the copy for 2 s, then fixes the 95th percentile plus 2 ms, at least 4 ms. The window's copies are
    ready in 0.2–1.4 ms, so it fixes 4 ms, and every frame becomes due 36 ms earlier, 3 at once. The
    moments jump 36 ms with the fixed delay too (`findings\pipeline.md`, "Stage 3: frame picker").
  - That is all of the 0.21–0.28% in synthetic runs. At Smoothest it falls before the runner starts
    counting.
  - Replays add single skips where the game's frames arrive bunched. One frame per tick shows a frame
    twice, then has to skip one when two are overdue. In the queued replay 2.5–2.7% of ticks repeat
    and 0.43–0.65% skip.
  - Drawing once per game frame (A) has neither.

**Tried and dropped:** switching off the planned edges' uncounted clamp against a stand-in image
(above). The jitter goes, and black borders come: 15% offline, 0.98% in e2e.

**Recommendation of the time** (no default changed):
- The window keeps drawing once per game frame (A). It misses no frames and has the calmest low-zoom
  view.
- For the OBS plugin, which draws on ticks with B's configuration: the slot-timeline picker (C) was the
  only option here that is no worse anywhere and better at low zoom (Smoothest jitter 1.58 → 0.98,
  Instant's limit 20.5 → 1.5%), with identical picks. It was a candidate for the plugin's default, after
  an OBS recording confirms it. Not checked: e2e doesn't run the plugin. At the default zoom 1.5, B
  already passes everything.
- Edges against the shown image were not ready. Before measuring them again, the 3-frame jump when the
  copy delay is fixed should go (or the edges' lead should follow the slot timeline's eased shift
  instead of its own average), and the planning should not chase the predictions' overshoot. This was
  done next (next section).

## The edges in the smoothing

**What changed.** Since this change nothing after the smoothing changes the view. Each smoothing keeps
it inside the image that is shown with it (`vr_mirror\core\pipeline\picture_room\README.md`).
- On ticks that is the game image standing in for the tick's moment. With the fixed delay it is drawn
  about 16–22 ms after that moment, and sometimes it is the previous one again.
- The one count of stops ("edge limit acted") covers it. Before, the clip against that image was a
  second, uncounted clamp after the smoothing (B, above).
- The planning checks each path moment against the image that will be shown then, as far after the
  moment as the shown frames run ahead. That lead comes from their timing, not their poses: the shown
  frame's moment minus the tick's, the median of the last 9 ticks, eased over 0.1 s.
- Drawing once per game frame is unchanged. `stage_replay --golden`: all 90 window runs byte for byte
  the same.

**e2e,** the mirror app with `output_clock=ticks` and the fixed delay (the plugin's configuration):
main before the change (B, 2 runs) against after (2–8 runs). The "before" stops include the uncounted
clip, read from the trace's `edge_repeat_clamps`.

| Measure | Before | After |
|---|---|---|
| low-zoom jitter/head, Instant | 3.39–3.40 | 2.68 |
| low-zoom jitter/head, Smooth (limit 1.2) | **1.52** | 0.51–0.60 |
| low-zoom jitter/head, Smoothest (limit 0.7) | **1.58** | 0.51–0.53 |
| low-zoom stops %, Instant | 32.6–32.7 | 27.1–27.2 |
| low-zoom stops %, Smooth (limit 0.5) | **12.9–13.1** | 0 (one frame in one of 8 runs: 0.08) |
| low-zoom stops %, Smoothest (limit 0.5) | **12.1–12.3** | 0 |
| low-zoom recorded view jitter, Smooth / Smoothest | 0.314–0.349 / 0.309° | 0.315–0.357 / 0.308–0.318° |
| black_borders (zoom 1.5) jitter/head, Instant | 2.75 | 1.75 |
| black_borders stops %, Instant | 14.0 | 6.2–6.4 |
| black_borders jitter/head, Smooth / Smoothest | 0.17 / 0.16 | 0.17 / 0.16 |
| black_borders stops %, Smooth / Smoothest | 0 / 0 | 0 / 0 |
| frames with black borders, any of these | 0 | 0 |
| failed | low zoom Smooth, Smoothest | none |

**Offline** (`stage_replay --pipeline --ticks 90 --input edge_test+heartbeat`, fixed delay):
- The stops fall the same way:
  - zoom 1.2: Instant 28.7 → 24.8%, Smooth 16.8 → 0%, Smoothest 16.5 → 1.45%
  - zoom 1.5: Smooth and Smoothest 0.39 → 0%, Instant 11.4 → 5.2%
- The small-move jitter at zoom 1.2 halves: Smooth 0.059 → 0.029°, Smoothest 0.058 → 0.025°.
- Some numbers get worse:
  - the jitter while still at zoom 1.2 (Smooth 0.011 → 0.020°, Smoothest 0.005 → 0.010°) and at 1.5
    (Smooth 0.0035 → 0.011°)
  - Instant's small-move jitter at 1.5 (0.014 → 0.028°)
- Per output, the still jitter comes from the first second, while the shown frames' timing settles. It
  also comes from single outputs at the fast shake's reversals, where the path is held at the edge of
  images the runtime predicted past the head (up to 0.29°, one output).
- With the slot-timeline frame picker nothing gets worse beyond 0.0005°, and Instant's stops at zoom
  1.2 go 1.41 → 0%.

**What's left.** Instant on ticks still stops on about a quarter of frames at zoom 1.2 (6% at 1.5).
With the fixed delay the shown image runs about 22 ms ahead of the tick's moment, about 7° in a
315°/s turn, which a filter without look-ahead can't plan for. The slot-timeline frame picker (C) took
that to 0, offline.

**Tried and dropped** (the same offline runs, zoom 1.2, fixed delay):
- **The lead found by comparing poses, averaged over about 1 s** (`ShownPictureEdges`).
  Stops at Smooth / Smoothest: 8.4 / 8.8%, all in the first 1.3 s while the average caught up (the
  plan's image up to 3.1° from the one shown). In e2e 3.0 / 2.5% (a fail), in the fast shake.
- **The same, reset to the median of the last 9 ticks when they differ by a quarter period.** Stops
  1.9 / 0.9%, still jitter 0.030 / 0.042°, because the resets move the whole plan at once.
- **The lead from the timing without easing.** No stops, but still jitter 0.040° at Smooth, because
  the plan jumps when the lead changes. Eased over 0.3 s: still jitter 0.014°, stops 1.5–3.1%. Over
  1 s: stops 5.1–5.4%.
- **Easing from the very first tick.** In 2 of 8 e2e runs the first tick showed a frame a period off,
  and the eased lead started there. So the view was stopped on the next 7 ticks, by up to 2.4° (0.58%
  of frames at Smoothest, a fail). The lead is the median itself for the first 9 ticks since.
- **Instant catching up against the followed image instead of the shown one.** The same within 0.02
  of jitter and 0.2 points of stops (e2e).

## Edges planned for with the look-ahead

**How.** With Smooth and Smoothest, the look-ahead window shows, for each moment up to a second ahead,
the smooth view and the pose the moment's image will be warped from.
- Where the view would pass 90% of the room there (the last-resort limit is at 95%), that moment's
  target becomes the margin point, weighted 10,000 against the head's 1.
- The path is solved again, up to three times. The jerk penalty eases the path out to the margin and
  back, ahead of time.
- The code is in `vr_mirror\core\pipeline\4_smoothing\lookahead_smoothing.cpp`, against the picture
  room. In older builds it was in the edges.

**`black_borders`' motion at Smoothest, by zoom** (jitter as a share of the head's, and the frames where
the last-resort limit acted):

| Zoom | before | now |
|---|---|---|
| 1.3 | 0.18, 0% | 0.18, 0% (planning acts on 0.2% of frames) |
| 1.2 | 1.06, 1.6% | 0.48, 0% |
| 1.1 | 2.18, 18% | 0.72, 7.7% |
| 1.05 | 3.01, 47% | 1.21, 20% |

- At 1.1 and below, the remaining clamps are where nothing in the window can help, for example the
  25° head tilt with the horizon held level.
- At the default zoom (1.5, and the 1.29 in use) nothing changes.
- Smooth, with half a second to see ahead, gets the clamps off at zoom 1.2 (1.6% → 0%), but is not much
  calmer: 0.85–1.05 against 1.02–1.05 before.
- **Checked against the images' poses.** Each moment's pose is the runtime's prediction for its slot,
  recorded for nearly all of the window by then (`PimaxSource::PredictedHeadsAt`). Checked against the
  measured head instead, the fake's prediction error (1.7° rms in these turns) left the limit acting
  on 1.5% of frames at zoom 1.2, and jitter at 0.89–0.97. With predicted poses switched off both agree,
  and it read 0.28 and 0%.
- **Tried and dropped: a correction shaped after the solve,** a smooth bump per moment, sized by an
  acceleration limit of 100–400°/s², the largest per direction. Each frame recomputed the bumps from
  its own window, so consecutive frames disagreed. At zoom 1.2 the jitter stayed at 1.05–1.10, 98% of
  it in corrected frames. In the solve, consecutive windows agree, as the plain look-ahead's do.
- **Tried and dropped: weight 100,000 with five passes.** Stiffer, so sharper: jitter 0.81 at zoom 1.2
  (0.48 at 10,000), and the clamps at 1.1 unchanged.
- **On real head poses** (4.3 h, `findings\mirror_app.md`, "Core's pipeline on the same poses"). At zoom
  1.29 nothing reaches the edge in play. At 1.2 the planning acts on 0.08–0.13% of frames, and the
  last-resort limit on 0.001–0.002%. At 1.1 it acts on 12–15%, and the view gets shakier than
  Instant's. So about 1.2 is as low as the look-ahead goes well.
- **Not covered:** whether an eased-in correction looks right in a recording.

## Smoothing compared with Instant: what it found

- **Fixed: with Smooth and Smoothest, a frame judged late made the view jump back.**
  - The view was computed for the slot the frame matching moved a late frame to (the one it was drawn
    for). But the frame is shown in its own slot, so on a moving path its view jumped back by a frame's
    motion.
  - Where frames were wrongly judged late (`replay_false_retiming_featureless_cockpit`, every frame
    really on time), Smoothest's view jumped by up to 0.35°, about 7 px at 1080p. Smoothed jitter:
    0.031–0.032° against Instant's 0.002°, 2.2 times the head's own.
  - In the real queue (`replay_queued_late_frames`), frames drawn one or two slots late jumped too:
    0.009° against 0.002° on time.
  - Instant wasn't affected, because its filter steps by the frames' arrival.
  - The fix: the view is for the slot each frame is shown in. In the code of the time that was
    `MirrorFrame::slotTime`, then the pipeline class's `ShownTime`. It is `DrawnPose::MomentOf` now.
    The image is still warped from the pose it was drawn with.
  - The featureless replay's smoothed jitter at Smoothest fell from 0.032° to 0.001°, and the queued
    replay's from 0.0035° to 0.0010°.
- **Dropped frames look no worse with the look-ahead.**
  - In a 60 fps recording, 5% dropped frames raise the view's jitter from 0.208 to 0.240° at Instant,
    and from 0.207 to 0.227–0.230° at Smoothest (three runs each). Most of what is left at every
    smoothing is the judder of 90 fps shown at 60.
  - Instant's own smoothing gets shakier with drops: 0.39–0.43 of the head's → 0.49–0.59. Smoothest's
    doesn't: 0.012–0.018 → 0.018–0.019.
  - The mirror app shows the frame before for a dropped slot, at every smoothing. No frame is made up
    (`longest tool frame gap` 33 ms: two drops in a row).
- **Fixed: with Smooth and Smoothest, more frames got the wrong pose in the two failing replays.** Five
  runs each: featureless 2.8–3.0% against Instant's 1.3–1.6%, queued 8.9–14.1% against 7.2–9.2%.
  - The cause was not the hold. The mirror app at Instant draws a frame at once, before its own image
    is judged, so it uses the previous frame's decision. A held frame got its own, as the OBS plugin's
    frames do (drawn a few ms after arrival).
  - Each decision came from the frames up to it only. On these replays a frame's own decision was
    wrong more often than its predecessor's.
  - The fix: a held frame takes its lag in hindsight, when it is drawn. It takes the cheapest path
    through every frame judged since. The lag filter keeps its back-pointers for 2.8 s
    (`vr_mirror\core\pipeline\2_drawn_pose\slot_check_drawn_pose_lag.h`), in the mirror app and
    the OBS plugin alike.
  - With the next fix too: featureless 0.1–0.2% at Smooth and Smoothest (worst frame 0.14° against
    Instant's 1.39°), queued 4.4–5.5% and 4.6–8.5% (Instant 6.9–9.7%).
- **Fixed: with Smooth and Smoothest, the mirror app showed 22% of frames with no slot decision.**
  - The hold made each frame's thumbnail on arrival. But the present that would submit it was an older
    frame's, and there was none at all through the first second. The thumbnailer reads only what the
    GPU has finished.
  - So the matcher's queue of six filled, and dropped frames: 165 of 733 were undecided in
    `replay_queued_late_frames_smoothest` (the first second, then runs of 6–12).
  - Found with the frame trace's `decided_lag` column of the time.
  - The fix: each arrived frame's copy and thumbnail are submitted at once, as the OBS plugin's copy
    thread does. 0 undecided. The OBS plugin wasn't affected.
  - The Smooth and Smoothest replay numbers before this fix understated the matching. So did the
    "tried and dropped" below, first measured then.
  - **Tried and dropped: judging the frames before the timing gate opens,** the other option for
    hindsight. The gate opens on about the third frame of a queue, and starts the filter afresh after
    the change to a lag. Judging the 6 frames read before it opened leaves `lag_sim`'s hindsight on the
    dust replay at 4.3%, as without. The pair across the change fits it hardly better than no change:
    0.015 against 0.018. Not kept.
  - `vr_mirror\recording_analysis\replica.py` prints both. On the dust clip with the true lags, the
    forward decisions re-time 12 of 779 on-time frames and get 38 of 101 late ones right. In hindsight:
    4 and 33. It is still no exact copy of core ("Real thumbnails in replays", below), so e2e decides.

## The slot check

### Fixed (the mirror app and the OBS plugin): when LMU falls behind, frames were warped from a pose that is too new

**The problem.** In a real race (dust clouds, traffic), the GPU-bound game can queue up to two frames.
Each frame is still rendered with the prediction for its own display slot, but reaches the mirror one
or two slots later, at the usual steady rhythm. The mirror app assigned it the later slot's pose, so
the image jumped by the head's motion over 11–22 ms. Measured: 0.2–0.3° at 12–33°/s, 4–6 px in a 1080p
recording. Reproduced by `replay_queued_late_frames`.

**The slot check.** Each frame's slot is checked against its image
(`vr_mirror\core\pipeline\2_drawn_pose\slot_check_drawn_pose_lag.*`).
- For each pair of candidate slots of two consecutive frames, both thumbnails are re-projected to the
  pose halfway between those poses, and compared. A forward filter picks the lag.
- The mirror app draws a frame before its own thumbnail is read, so the first frame of each change
  keeps the old lag there. The OBS plugin draws 4 ms after arrival, in time for each frame's own
  thumbnail.
- On the smooth fake image, before the fine texture, the replay had 95 of 101 late frames right. That
  image didn't blur, so it overstated the matching.

**The timing gate.** A frame may get a lag only while the frame timing shows a queue: the slot lead,
modulo a display period, more than 3 ms off its usual value over 5 frames, within the last second.
Otherwise the right lag is 0, and on real images the matcher picked wrong lags when it ran then.
- **Tried and dropped: also opening on dropped frames.** On any drop, the gate was open 34% of a race,
  with false re-timing while cornering. On two drops within 0.5 s, a three-frame hiccup opened it and
  75 on-time frames were re-timed, visible as jitter.
- On the phase only, the gate is open 1–4% of a session, shut through both of those, and open from the
  third frame of the real queue.
- Without the gate the synthetic scenarios improved too (worst frame: `fast_sweeps` 4.3° → 0.29°,
  `racing` 3.9° → 1.3°), because the fake game renders with a slightly different pose than the mirror
  app records. With the gate they are close to before (2.5°, 1.7°), as their timing is calm.

**Fixed: the matching took on-time frames for late ones on real images** (jitter while cornering, at a
short hiccup, and in normal driving).
- It re-projected only the previous thumbnail, by a fraction of a texel, which blurred it. So the
  smallest rotation always matched best.
- On thumbnails rebuilt from four recordings, the image's motion fitted 0.10 of the head's instead of
  1.0, and 132–612 on-time frames per window were re-timed (without the timing gate).
- Since, both thumbnails are re-projected halfway, to the pose between the two: fit 1.00, 0–2 false
  re-timings per window, none with the gate.
- The e2e fake only showed this once it drew fine texture ("Replaced: imitating real images in the
  fake", below).

**Penalties,** tuned on the rebuilt real thumbnails (scored against the lags fitted to the videos) and
checked in e2e: 0.03 deg² per lag change, and 0.004 deg² for going back to lag 0.
- Real dust queue: pose error 0.076° rms, against 0.130° doing nothing and 0.164° with the first build.
- No false re-timing in the cornering, the hiccup, and session C's 0:34 windows.
- e2e: dust 3.2% from the wrong pose, worst frame 0.20°.
- **Tried and dropped:**
  - 0.004 per change, tuned on the smooth fake. Noise switched the lag on real images.
  - 0.015. Dust 0.064° rms, but 10 on-time frames re-timed at 0:34.
  - Leaving lag 0 at 0.03, but other changes at 0.004. 35 false re-timings while cornering.
  - Going back to lag 0 at 0.004 without that: the frames held after the queue went 27 → 12.
  - A rule that a frame can fall further behind only across a gap in the arrivals, as a penalty. The
    game renders each frame for a later slot, and the fitted lags obey the rule in 1080 of 1084 steps.
    It was better on the real dust queue (0.068° rms, cornering without the gate 2 → 0 false), but
    worse in e2e (4.8%, and 5.3% with the cheap return to 0). The fitted lags break the rule at the
    start of the queue, and once the filter wrongly went back to 0 it could not fall behind again.

**What is left wrong on the dust replay:**
- the queue's first 4–6 frames, because the timing gate opens from its third frame
- frames after the game changed its lag while the head barely moved (0.1–0.2° per frame)

The images show a lag only in the one pair of frames where it changes. With a constant lag, consecutive
frames look alike whatever the lag. So a change that pair doesn't show is missed until the next one,
and the filter stays a change behind: here 10–12 on-time frames held one slot late after the queue.
The error of those frames is small, because the head is slow then: mostly 0.05–0.2°. Not tried:
comparing more than two frames, so a change shows in several pairs.

### Fixed: false re-timing where the cockpit is featureless

Jitter in a slow corner at 18:31 of race D (`replay_false_retiming_featureless_cockpit`).
- **The problem.** A one-frame hiccup opened the gate. The image's detail was in the scenery and cars
  through the windscreen, which move on their own, and the dark cockpit was almost featureless.
  Featureless texels match any rotation, so they filled the better-matching half, and the evidence was
  noise. The matcher re-timed 23 on-time frames in bursts of −1, −2 and −3 that snapped back to 0.
- **The fix.** The 30% of the overlapping texels with the least detail (local gradient) are left out
  before the trimming.
- **On thumbnails rebuilt from the recordings,** with the gate (`vr_mirror\recording_analysis\replica.py`,
  which matched the plugin's choices there: 25 frames against its 23):
  - the slow corner: 25 → 5 frames, all one slot off
  - the real dust queue: 11 → 0 on-time frames re-timed, and late frames found 43 → 49 of 101
  - without the gate, cornering and the 1:57 hiccup: 13 → 0 each
- **e2e:** dust 2.8–3.0% from the wrong pose (3.3% before), worst frame 0.15–0.24° (0.16–0.21°
  before). In the slow corner, one frame re-timed by one slot (0.22°).
- The worst-frame limit of both replays went to 0.25°, as agreed when it was set. It depends on whether
  a capture lands on one of the few wrong frames.
- **Tried and dropped:**
  - Leaving out 50%. The slow corner was fully clean, but e2e's dust replay found the queue a frame
    later: worst frame 0.26–0.29°, a fail.
  - Leaving out 40%. Dust 4.2%.
  - A threshold on the rotation sensitivity. No use: 1.2e-4 in the slow corner, against 1.5e-4 in the
    dust queue.
  - Making the fake reproduce the slow corner: a textured windscreen turning past at 27°/s with a
    plain cockpit, then a turning steering wheel as well. The fake's cockpit (its smooth colour
    gradient and the HUD panels) still lined up cleanly, and the matcher stayed right (0 of 94 judged
    frames re-timed). So the replay passes on every build.

### Why the dust replay fails

`replay_queued_late_frames` has LMU two frames behind for 68 frames, then one behind for 33, then on
time again. The true lags, fitted to the recording: −2 from frame 630873, −1 from 630941, 0 from
630974. `lag_sim` runs core's frame matching on the replay's exact inputs, deterministically, and
agrees with the mirror app in e2e on 97% of frames at Instant.

- **The thumbnails show a lag mainly where it changes, and here they barely do.** With a constant
  lag, consecutive frames look alike whatever the lag. Only the pair across a change differs.
  - The cost of the true change against no change, at the three changes: 0.015 against 0.018 at
    630873 (before the timing gate opens), 0.095 against 0.086 at 630941, and 0.188 against 0.180 at
    630974. So at two of them "no change" fits better.
  - Inside the queue they tell lag 0 from lag 1 or 2 (630881: 0.098 against 0.053), but lag 1 from
    lag 2 rarely, by 0.002–0.008 either way.
- **Hindsight can't recover it.** With a second of frames after each (Smoothest), 4.3% stay wrong: lag
  1 for the queue's first 23 frames, the 2 → 1 change found 10 frames late, and the 1 → 0 change 4
  frames late. After a delay of 5 / 10 / 20 frames: 6.6 / 5.4 / 4.3%.
- **The timing doesn't tell either.** Lag changes come at frames with a two-period gap or a slot jump.
  But most such frames inside the queue change nothing (630889, 898, 912, 915, 940, 943, 949–951, 957,
  963), and the change at 630941 comes a frame after one. Allowing changes only there reaches 3.9%
  offline, with no margin. The slot lead overlaps for all three lags (19–31 ms in the queue, 23–27 ms
  after it), as found before ("Tried and dropped: telling late frames by timing alone", below).
- **What could: more detail per frame.** The thumbnails are the plugin's own size, 64×32 of the middle
  of the eye. 128×64 would cost the matching about 4× the CPU. The replays' thumbnails would need
  rebuilding at that size from the recordings (`export_thumbs.py`), to test it in `lag_sim` first. Not
  tried.

The featureless cockpit at Instant is the same limit from the other side. From the frames before each
one, the dark cockpit's noise re-times 1.6% (`lag_sim`). 5 frames of hindsight take it to 0.4%, and
Smooth and Smoothest to 0.1–0.2%.

### Tried and dropped: telling late frames by timing alone

- The latency from a slot's announcement to the frame's arrival is normally 8.7 ms, 99% within
  6.7–10.8 ms. It drifts when the game queues frames, and a tracker following it halved the error in
  the replay (worst frame 0.24°).
- But the queue can grow or shrink without any visible gap (two frames sharing a slot at the start).
- Over all 23 drop windows of two race recordings, it was no better than doing nothing: a cockpit step
  error of 0.958 px against 0.946, with 0.857 the best possible.
- The same recording also wobbles at 18:34.5 without any late frames. That isn't explained by pose
  timing, and is still open.

### Tried and dropped: poses from the arrival timing alone

**The idea.** The shake study of two later races (the data folder's shake in recordings)
found the frame matching re-timing frames all race long while the game ran under 90 fps: 278 visible
jumps a minute, against 11 at 90 fps. Without the matching, a frame the game drew late would be warped
from a steady wrong pose, which isn't seen, instead of flipping between slots.

**Measured** with the setting `drawn_pose` off against on (replays, four runs at 320×180, visible
pose jumps):

| Replay | Matching on (Instant / Smooth / Smoothest) | Off (all three) |
|---|---|---|
| queued_late_frames | 1.8 / 0.8 / 1.5% | 5.0–5.6% |
| false_retiming_featureless_cockpit | 1.9 / 0 / 0% | 0–0.2% |
| false_retiming_head_turns, short_hiccup | 0% | 0% |

**Why it fails.** In the dust queue the offset stays at the true two slots for every frame. Yet the
error changes by 0.1–0.6° from frame to frame (`jumps.py` in the data folder's shake in
recordings, on `out\replay_queued_late_frames`). It is the head's motion over two slots, and the real
head and the runtime's per-slot predictions vary that much within tens of milliseconds. A wrong slot is
never a steady error. So the default stays on. The setting is kept for comparing in a real recording,
where the game runs under 90 fps for a whole race, which no replay covers.

## The cockpit correction

The setting `drawn_pose` 2: the slot check, plus a correction of each frame's pose measured from
its fixed cockpit while frames arrive irregularly (`vr_mirror\core\pipeline\2_drawn_pose\README.md`,
"The cockpit correction").

**Measured** in `queued_cockpit`, against the slot check alone in `queued_cockpit_slot_check`. Two runs
each at 320×180, the second within the full suite: all 36 runs and 12 comparisons
passed, and the replays were within their earlier ranges. Instant / Smooth / Smoothest:

| Measure | Slot check alone (today's default) | With the cockpit correction |
|---|---|---|
| visible pose jumps (the error changing by over 0.1° from a frame to the next) | 29.3–30.0 / 12.5–12.7 / 13.0% | 1.0–1.2 / 1.1–1.2 / 1.1% |
| largest jump | 1.87 / 1.85 / 1.85° | 2.01 / 2.01 / 2.00° |
| pose error | 1.05 / 0.98 / 0.99° rms | 0.79 / 0.81 / 0.82° rms |
| correction drift (its mean over the run) | – | 0.09–0.10 / 0.10–0.16 / 0.10° |
| slow pose error (largest 1 s mean) | 1.91° | 2.17° |
| recorded view jitter | 0.243 / 0.216 / 0.215° | 0.228 / 0.211 / 0.217° |
| game frames missed | 0% | 0% |

- **The drift measure.** In older builds e2e graded the correction's own mean ("correction drift",
  0.08–0.10° in `queued_cockpit` then). The pose error's mean replaced it as "pose drift". It read
  0.29–0.31° in the same runs, and 0.19° with the slot check alone.
- **What is left:**
  - The first frame of an irregular stretch. The correction fades in from nothing, so a lag change at
    the drop that starts the stretch shows.
  - Slot errors so large (3–5° in the fake's fastest glances) that the correction is pulled back faster
    than 3°/s.
  - "Frames from the wrong pose" stays high (90%). The correction keeps the image where the previous
    frame had it, and returns to the head poses slowly, so a frame's pose is rarely exactly the one it
    was drawn with. What the measure counts as wrong is a steady offset, which isn't seen. The slow
    pose error is a little higher for the same reason.
- **The fake is harsher than the real races here.** While the game is behind, the mirror app's arrival
  timing puts its frames a slot later than the game's own (arrivals 4 ms late). So a frame two slots
  behind is warped from a pose three slots off, and the fake's predictions overshoot in the glances
  (137°/s). That gives slot errors of 3–5°, against the 0.2–0.6° measured in real races.
- **Its time:** 0.36 ms of the measuring thread per frame while it corrects (`tool.log`: "cockpit
  correction: ..."), and nothing while frames arrive regularly.
- **The replays can't judge it.** They hold only 64×32 thumbnails of the middle of each real frame,
  and the patches it measures are 4×4 game pixels per texel across the whole eye. They run with the
  default (the slot check), as before.
- **Not checked:** a real recording (the plugin's own log and trace), and real cockpits, since the
  fake's has more detail than a dark real one. A test on real images is still to be built: raw,
  unstabilized footage of a race at a steady 90 fps (to be recorded), with its frames played with the
  82 fps queue applied.

## Real thumbnails in replays

### Replaced: imitating real images in the fake

- Before replays brought the real thumbnails, the fake drew things real images have:
  - a swaying "scenery" band
  - sharp cockpit HUD panels
  - fine cockpit texture: contrast towards grey, spared where the runner reads the view
- Each caught one mistake. The band and the panels caught the first matching build's (misled by moving
  content). The texture caught the interpolation blur of one-sided re-projection (the old matching
  9.1% from the wrong pose on the dust replay).
- The next real failure, a featureless dark cockpit in a slow corner, couldn't be imitated. A textured
  windscreen turning past at 27°/s with a plain cockpit, then a turning steering wheel as well: the
  fake's cockpit still lined up cleanly (0 of 94 judged frames re-timed).
- So replays show the game's real thumbnails, and the imitations were removed.
- With the texture, texture everywhere made the view reading noisy (worst frame 0.11° → 0.19°), and a
  weak texture no longer misled the old matching (2.0% against 9.1%).

### Open: with real thumbnails, two replays fail on the build of the time

Checked offline with `vr_mirror\recording_analysis\replica.py` on the source recordings, under the real
session's timing from the trace.

- **The thumbnails are faithful to the recording.** On the featureless corner (356 frames, 94 judged by
  the timing gate), the on-time frames re-timed with the same matching:

  | Thumbnails | compared as displayed | compared in linear light (as core) |
  |---|---|---|
  | rebuilt from the video, averaged as displayed | 5 | – |
  | rebuilt from the video, averaged in linear light (as core) | 16 | 62 |
  | exported (`.thumbs.png`), covered texels only | 16 | 61 |
  | exported, with the export's fill (what e2e shows) | 23 | 66 |

  - Rounding to 8 bits doesn't matter (5 → 1, 16 → 14).
  - The fill matters a little. The recording covers about 80% of each thumbnail, and the export makes
    up the rest smoothly, fixed to the recorded crop instead of moving with the head. That is 5–7 more
    false re-timings here.
- **`featureless_cockpit`: e2e repeats the real session's own failure.** The mirror app in e2e went
  wrong at frames 404839–40, 404852–59 and 404884–85. The plugin in the real session went wrong at
  404838–39, 404850–56 and 404880–85. All candidate lags match about equally there (for example 0.127,
  0.133, 0.144 and 0.121 at 404854), and the matching follows noise in the dark cockpit.
- **`queued_late_frames`: the mirror app switches lag late when the game catches up.** True lags: −2
  from frame 630873, −1 from 630941, 0 from 630974. The mirror app kept −2 to about 630969, and −1 to
  630986.
  - At the −2 → −1 change the head moves 0.07–0.12° per frame, and every candidate costs about the same
    (0.020 at 630949), under the 0.03 change penalty. The images don't show the change, offline as in
    e2e.
  - The −1 → 0 change shows clearly (cost 0.22 at 630974), and the replica switches at once.
  - Why the mirror app in e2e holds −1 for 13 more frames is not known.
- **`replica.py` is not an exact copy of core.** It compares thumbnails as displayed. Core averages and
  compares them in linear light.
  - So its "5 frames" for the featureless corner was too optimistic, and the change penalties were tuned
    on that comparison.
  - Comparing in linear light it re-times 62 frames, where the mirror app in e2e re-times 13 on the
    same thumbnails, with the gate open for the same 94 frames. Another difference is still to be
    found.

## The test harness

- **Changed: a small window, a sharper border check, and the stall rule.**
  - The mirror app runs at 320×180 in the tests (`reference\e2e_results.md`, "Window size").
  - Edge and corner patches are judged by their outermost line, not by 8% of the patch. That catches
    1.6 px of black instead of 3.2–4.8 px.
  - The fake can draw less than the field of view it reports (`drawn_fov`), so black borders can be
    made on purpose.
  - The stall rule counted the `black_borders` shake's three turnarounds as stalls: the view slows to
    about 25°/s as it reverses with the head, within a frame. It counts only a catch-up in the same
    direction since.
- **Dropped: synthetic edge cases.** Dropped frames, timing noise, fast and slow looks, a
  hand-held headset, and stabilization off (the chain's accuracy) were synthetic tests. Edge cases are
  replays of real footage since, and the synthetic tests became the ones listed in
  `vr_mirror\e2e\README.md`.
- **Run the suite with LMU and the mirror app closed.** With LMU running, results varied up to 2–3×
  between runs (fast turns 0.16° against 0.30°), and wrongly suggested that beta 2 adds jitter.
- **Windows' window capture of the mirror app delivers about 55 fps** (a 164 Hz monitor divided by 3),
  not the mirror app's 90. A 60 fps OBS recording through Window Capture repeats about 10% of frames
  during head motion. That is a capture limit, not the mirror app's.

## The first mirror app

- **Fixed: the mirror app missed 1–2 game frames per second.** The frame signal is off for only about
  1.5 ms per frame, and `Sleep(1)` overslept it. Polled every 0.25 ms since: 0 missed.
- **Fixed: stall-and-catch-up after fast turns.** The lag limit was applied outside the filter.
- **Fixed: black borders during head movement** (41% of frames in fast turns, 62% in racing).
  - **Tried and dropped: auto zoom.** It zoomed in for 76–97% of frames while the head moved.
  - **Tried and dropped: pinning the view to "head + room" near the edge.** It passed head jitter
    straight through.
  - **Kept:** the smoothing catches up faster near the edge, scaled to the head's turning speed.
  - Zoom 1.2: 0% black and a lag up to about 6°, but too little room for visible smoothing (tested in
    the headset).
  - **Zoom 1.5** (the default then): 0% black, a lag up to about 8–12° (about 20° before), and no
    stalls. Tremor is filtered as before. Racing vibration is at 34% of the head's (23% with plain
    smoothing).
- **Beta 2.0** (the default, chosen by feel): the edge catch-up acts in no scenario. At beta 0 it
  caused a sharp speed-up after the start of a turn.
  - Clean runs (LMU closed, twice): slow looks about 0.018°, fast turns about 0.093°, racing vibration
    about 33% of the head's, and a lag up to about 7–10°. The still head and tremor are fully filtered.

## Open question: the fake game's pose

The fake game draws each frame with the pose predicted about 23 ms ahead, taken when its display slot
is announced. That is an assumption.
- On screen, the mirror app's pose-at-arrival correction then misses part of fast tremor: an on-screen
  wobble ratio of about 0.6 for hand tremor, even though the filter itself removes 99%.
- In the real headset, "frame-matched" timing, which assumes exactly this model, looked worse than
  arrival time.
- So LMU probably renders with a different pose, and the on-screen numbers aren't trustworthy until the
  fake is calibrated against real frames.
