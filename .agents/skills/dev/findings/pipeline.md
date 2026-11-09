# Findings: the frame pipeline's stages

Terms: `reference\terms.md`. What each stage does: `vr_mirror\core\pipeline\README.md` and the stage's
own README. The e2e side of most findings here is in `findings\e2e.md`.

"Lower is better" holds for every measure in this file unless a row says otherwise.

## Stage 1: arrival timing

### Timing noise

- Raw arrivals wobble by about 0.7 ms rms.
- Presenting a fixed delay after the de-jittered arrival spaces the mirror app's presents to about
  0.13 ms rms in e2e, against about 1 ms when presenting right away
  (`vr_mirror\core\pipeline\1_arrival_timing\README.md`, and `vr_mirror\mirror_app\README.md`, "Paced
  presents").
- The noise removed is logged every 12 s ("arrival timing noise removed"). Pimax Play running pushed
  it from 0.52 to 1.46 ms rms in one e2e run (`reference\e2e_results.md`, "Results on the current
  build").

### Fixed: a single late frame takes the free slot before

Rounding to the nearest slot gave a frame that arrived more than half a period late the *next* slot.
The frame after it then took that slot too. Two frames sat on one slot, and the first was lined up
from the next slot's head pose: a one-frame jolt, 3.5 px at 1080p at 7:39 of race F
(`vr_mirror\e2e\replay\late_frame_same_slot.csv`).

The rule: a frame falling more than 0.2 periods (2.2 ms) before its nearest slot gets the slot before,
when both hold:
- that slot is still free (the previous frame is on an earlier one)
- the previous frame was on time (within 0.15 periods of its slot)

Measured over 4.4 h of traced driving and in e2e:

| Measure | Nearest slot | The rule |
|---|---|---|
| Two consecutive frames on one slot, race F / session E traces | 87 / 150 | 52 / 116 |
| Frames given another slot, race F / session E | – | 122 / 152 (0.02% / 0.02%) |
| e2e `replay_late_frame_same_slot`, frames from the wrong pose, Instant / Smooth / Smoothest | 1 / 1 / 1 (0.17°) | 0 / 0 / 0 |
| e2e `replay_queued_late_frames_smooth`, frames from the wrong pose, several runs | 4.0–5.7% | 4.1–4.9% |

The pairs left, race F / session E (`pairs_left.py` in the data folder):
- a late frame right after another late frame, which the rule leaves alone on purpose: 26 / 80
- two frames arriving almost together, the second 1.5–2.6 ms after the first (median), both rounding
  to one slot: 24 / 27
- a late frame whose slot before was taken: 2 / 9

Most come in rough stretches: 39 of 52 and 98 of 116 have another late frame among the five before.
Session E's recording has 12 pairs, which showed no jolt because the head was too still. Race F's
recording has one pair, at 7:39. Not checked: whether the drawn pose stage catches them.

**Tried and dropped on the way:**
- **Without the "previous frame on time" condition.** It took more pairs (87 → 29, 150 → 56). Every
  moved frame whose true slot session E's video tells (19) was indeed drawn earlier. But many of them
  were frames in a queue: 8 of the 19 were three slots early. There the drawn pose stage counts its
  lag from this stage's slot, so the two corrections added up. e2e `replay_queued_late_frames_smooth`
  went to 7.0–7.3% from the wrong pose (limit 7%, failing), against 4.0–5.7%.
- **Also never giving a frame the previous frame's slot (giving it the next one then).** It moved
  550–2,000 frames forward to remove the same pairs, each moved frame pushing the next. In a burst of
  queued frames the truth is the other way: they were drawn for earlier slots.

The scripts and numbers are in the data folder's investigation of the stutter with smoothing 2 (`snapping.py`,
`snap_rule.py --steady 0.15`, `snap_truth.py`).

### The reported slot and pose

`ReportedArrivalTiming` and `ReportedDrawnPose` serve the OpenXR layer, whose frames carry the
display time the game drew them for, and its pose.

- **Tried and dropped: the drawn slot as the frame's slot,** with its de-jittered arrival that slot
  minus the running median gap from arrival to slot.
  - In `stage_replay` it matched the default: no frame from the wrong pose.
  - In e2e (the fake game with the layer's frames) it broke as soon as the game's timing did. When the
    game falls a slot behind, the gap changes by a period and the median flips at once. One frame's
    step then jumps by about 11 ms. `openxr_game_behind` at Instant: jitter 13.5 times the head's, a
    stall, and 1.9% of frames missed.
  - With the de-jittered arrival measured on the grid instead, the drawn slot as the frame's moment
    still put frames off the arrival rhythm. A frame seen a period late (its copy's marker checked a
    frame later, after a dropped one) kept its early moment while it was shown a period later. The
    look-ahead's path then ran through moments out of step with the images.
    `openxr_dropped_frames_smooth`: jitter 0.93 of the head's (limit 0.1, Pimax 0.02).
- **What replaced it, as for Pimax:**
  - Stage 1 gives the arrival measured on the display grid (the phase's circular median), and the
    slot the frame is shown in at that rhythm, which is the drawn slot when the frame is on time.
  - Stage 2 takes the pose of the drawn slot, which travels in the frame's record (`drawnSlot`).
  - `stage_replay --pipeline --arrival_timing reported --drawn_pose reported`, with each frame
    reporting the slot it was really drawn for: no frame from the wrong pose in any input at Instant or
    Smoothest, and the same lag and edge numbers as the default.
  - In e2e all 15 OpenXR runs pass, every frame from its exact pose.
- **Open: the horizon's worst tilt with the look-ahead.** In the OpenXR scenarios, Smooth and
  Smoothest tilt the horizon 0.05–0.14° more at their worst than Instant. That is beyond the 0.05° the
  comparison with Instant allows.

  | Scenario | Instant / Smooth / Smoothest, worst tilt |
  |---|---|
  | `openxr_stabilization` | 0.22 / 0.29 / 0.26° |
  | `openxr_dropped_frames` | 0.18 / 0.26 / 0.26° |
  | `openxr_black_borders` | 0.64 / 0.63 / 0.78° |
  | The Pimax scenarios, for comparison | 0.23 / 0.21 / 0.22° |

  - Typical frames are more level than Pimax's: a median of 0.035–0.041° against 0.07°.
  - It isn't tied to the head's speed. A four times smaller fake image (less averaging in the store)
    doesn't change it.
  - Not explained.
  - With the view's camera levelled itself (below, "Fixed: the same zoom showed a narrower view"), all
    four comparisons passed in one full run: worst tilts 0.15–0.24° at every smoothing
    (`reference\e2e_results.md`, run 6). The fake OpenXR eye is turned 1°, so the old camera tilted
    with the head's pitch, and the look-ahead's pitch differs from Instant's. That is the likely cause.
    One run only, so not settled.
  - The next full run showed the same, with `openxr_game_behind` failing too (0.175 / 0.259
    / 0.256°). Which look-ahead setting is worst changes from run to run (`openxr_dropped_frames`
    0.198 / 0.314 / 0.221°). See `reference\e2e_results.md`.

## Stage 2: drawn pose

- **`stage_replay --stage drawn_pose`** (`reference\e2e.md`): its numbers when it was made equal
  the lag scores before. `queued_late_frames`: 8.6 / 8.3 / 4.3 / 4.3% wrong, at the mirror app's Instant,
  the plugin's Instant, half a second later and a second later.
- **The cockpit correction, measured** in e2e `queued_cockpit` (the game behind at 82 fps with a
  detailed cockpit, `findings\e2e.md`, "The cockpit correction"): visible pose jumps on 1.1–1.2% of
  frames, against 12.7–29.3% with the slot check alone. The replays hold only 64×32 thumbnails of the
  middle of the eye, so they can't judge it.

### Tried and dropped for the cockpit correction

An offline bench of the e2e fake's cockpit and queue, 15 s, measured in visible jumps:
- **Patches at fixed areas in both frames.** A turning head carried the cockpit out of them, by 12–23
  texels a frame in fast glances, and every patch failed there. The moved second set of patches took
  the jumps from 10.6% to 5.6%.
- **Measuring from the slot check's pose without the last correction.** Every step then had to find
  the whole correction (1–2°), beyond the search.
- **The fade scaling the filter itself.** Fading compounded frame by frame: the correction shrank by
  the fade's share each frame, a jump of 0.1° a frame at fade-out. The fade scales only what is shown.
- **A step limit from the head's own turn over four slots.** The predictions of neighbouring slots
  differ by up to 0.4° in head tremor where the head moved 0.05°, so true steps were refused.
- **Pulling back at the time constant's full speed.** 6.3% visible jumps, against 1.4% at most 3°/s.
  The pull itself glided the image by 0.1° a frame, and more where the slot error was 3–4°.
  - Longer time constants (0.5–2 s) without the limit: 2.7–1.5%.
  - With no pull back at all, the correction drifted 3.4° on average.
- **The rotation agreed on from the median yaw and pitch alone.** Steps with roll were refused.
- **No robust weights and no record of disagreeing patches.** With the head still while the car
  cornered, the correction crept 0.12° a frame towards the scenery past the windscreen's edge.

### Fixed: after hiding and showing the OBS source, the stage saw each frame's image late

- **What happened.** The OBS plugin makes a new D3D device each time the source is shown. The feed
  (`vr_mirror\core\adapters\d3d\drawn_pose_feed.*`) kept its thumbnail and patch makers from the old
  device, and used them with the new device's context. So the stage saw each frame's image only 1–2 s
  after it arrived.
- **Its effect.** In race F (hidden, then shown 10 s later), the frames shown at Smooth's 533 ms delay
  found their own judgement on 0.2% of ticks, for the rest of the session. Records turned up 1.2–2 s
  after arrival, so only frames shown during pauses had one. Each frame fell back to the slot judged
  last.
- **The other traces.** The four other traced sessions of the same build had no hide and show, and
  found their own judgement on 100% of ticks (one on Smooth included).
- **A crash, maybe related.** OBS also crashed on closing after race F, and only then. Not checked
  whether this was why: there was no debugger to read the dump.
- **The fix.** `Init` with another device makes the images afresh, and the ring's `Stop` releases them
  (`DrawnPoseFeed::Release`).
- e2e can't show it, because the mirror app makes its device once. `judged_scan.py` in the data
  folder's investigation of the stutter with smoothing 2 checks a trace.

### Other findings

- **The moment is the slot a frame is shown in,** not the earlier one a late frame was drawn for.
  With the earlier one, a re-timed frame's view jumped back along the path by a frame's motion: up to
  0.35° in `replay_false_retiming_featureless_cockpit` at Smoothest (`findings\e2e.md`, "Smoothing
  compared with Instant: what it found"). The moment has since become plain frame data
  (`FrameTimes::moment`) instead of being worked out from the frame's lag, with the same numbers
  (`stage_replay --golden` byte for byte).
- **Tried and dropped: the arrival slot as the measured head's moment** (`stage_replay --pipeline
  --drawn_pose measured`). The measured pose is the head at the pose time, about 22 ms
  before the slot. So the look-ahead's view trailed the image by that much head motion:

  | Input | Measure | Before | After |
  |---|---|---|---|
  | `edge_test+heartbeat`, zoom 1.2, Smooth | Edge limit | 0% | 16.1% |
  | | Small-move jitter | 0.022° | 0.334° |
  | | Lag p99 | 4.48° | 5.49° |
  | `racing+heartbeat` | Small-move jitter | 0.002° | 0.027° |

  So a drawn pose says where its poses sit (`MomentOf`).
- **The path's moments are checked against their predicted pose.** Against the measured head, the
  predictions' own error (1.7° rms in `black_borders`' turns) left the last-resort limit acting on 1.5%
  of frames at zoom 1.2 (`findings\e2e.md`, "Edges planned for with the look-ahead").
- **`MeasuredDrawnPose::PosesAt` copied the window's heads** instead of asking again, because a live
  `PimaxSource` can extrapolate differently between two calls. (The measured drawn pose has since
  been removed, below.)
- **Merged from two stages.** Before, the frame matching (stage 2 then) handed hosts a lag
  and a pose correction per frame. The hosts kept them beside each frame, and the drawn pose (stage 4
  then) turned them into a pose. The lag also set traces' columns and a stand-in image's lead. Since,
  one stage answers with the pose. `stage_replay --golden` was byte for byte main's after the merge.
- **In `findings\e2e.md`:**
  - "Fixed (the mirror app and the OBS plugin): when LMU falls behind, frames were warped from a pose
    that is too new", with its tried-and-dropped penalties and gates
  - "Why the dust replay fails": the 64×32 thumbnails can't tell the lags apart there
  - "Fixed: with Smooth and Smoothest, more frames got the wrong pose": hindsight for held frames

## Stage 3: frame picker

- **Identical to the plugin's own picking before this stage,** tick by tick. Checked with
  `vr_mirror\e2e\stage_replay --stage frame_picker`, at each smoothing:
  - on two OBS traces, of 54,444 and 403,469 ticks
  - on synthetic ticks at 50, 59.94, 60 and 144 Hz, with the smoothing changed mid-run

  Compared: the entry, the kind, the delay, the moment, the tick the delay is fixed, and the entries in
  use. 0 differences. The copy of the plugin's code it was checked against was removed after.
- **`kHeld` never happens as written.** When nothing due is ready, the last shown frame is still in
  the store only if it is among the ordered entries, and then it is picked as a repeat. 0 held ticks
  in all of the above. It is left as it is, because it is the plugin's behaviour. A store whose shown
  entry can be ready but excluded would need it.
- **The ring's size follows the delay:** the frames in the delay plus 3, at least 4
  (`vr_mirror\obs_plugin\README.md`).
- **The fixed delay's moments trail their images and jump once. The slot timeline's didn't**
  (`stage_replay --stage frame_picker --frame_picker K`, the shorter of those OBS traces and
  synthetic ticks). The slot-timeline picker has since been removed (below).

  | Measure | Fixed delay | Slot timeline |
  |---|---|---|
  | The shown image's slot minus the tick's moment, median, Instant / Smooth / Smoothest | 13.4 / 23.0 / 23.0 ms | −8.4 / 1.3 / 1.3 ms |
  | The largest jump of the moments, apart from the ticks' own step | 30–35 ms | 0.8–2.3 ms |

  - Negative means the tick's moment is after the slot, with an older image standing in.
  - The fixed delay's jump is at the tick the copy delay is fixed. 40 ms is assumed until then.
  - The picks themselves are identical, run for run: the same next, repeat, jump, held and none
    counts, and the same tick and delay fixed.
- **In e2e on the mirror app's ticks** (`findings\e2e.md`, "Drawing on ticks"):
  - The shown image's drawn slot runs 16.2 ms ahead of the tick's moment with the fixed delay, and
    −5.6 ms (half a period) with the slot timeline.
  - With the edges of that date, low-zoom jitter as a share of the head's at Smoothest: 1.58 with the
    fixed delay, 0.98 with the slot timeline, and 0.39 for the window drawing per frame.
  - Instant's edge limit: 20.5% against 1.5%.
  - It was a candidate for the plugin, after an OBS recording.
- **Fixing the copy delay skips 3 frames once,** with either picker. 40 ms is assumed for 2 s, then
  the 95th percentile plus 2 ms (at least 4 ms) is fixed.
  - With copies ready in 0.2–1.4 ms (the mirror app), every frame becomes due 36 ms earlier, 3 at
    once (a `kJump`).
  - That is all of the 0.21–0.28% missed in e2e's synthetic tick runs.
  - The plugin does the same once per start (30–35 ms on the traces).
  - The slot timeline kept the view from jumping there, but not the image.

## Stage 5: warp

- The image is warped from the pose the game drew it with, not from the measured head
  (`findings\e2e.md`, "The slot check": "Fixed (the mirror app and the OBS plugin): when LMU falls
  behind, frames were warped from a pose that is too new").

### Fixed: the same zoom showed a narrower view through OpenXR than through Pimax

- **Seen:** a recording with OpenXR Mirror at zoom 1.25 and Smooth looked more zoomed in than one with
  Pimax Mirror at the same settings (#5). The frame trace confirmed zoom 1.25 on every frame.
- **Cause:** the view looked along the eye's own axis and was as wide as the image's narrower side
  allows, divided by the zoom (`GeometryFor`). The two sources describe the same left eye image from
  different axes:
  - Pimax gives its tangents from the eye turned 2.0° outward: 50.1° out and 49.6° in.
  - LMU through OpenXR gives the game's own field of view, from straight ahead: 52.6° out and 47.6° in.
  - Turned by the 2.0°, Pimax's are 52.1° out and 47.6° in, the same image within 0.5°.
  - At zoom 1.25 the Pimax view was 86.4° wide, looking 2.0° outward. The OpenXR view was 82.4° wide,
    looking straight ahead, with 11.4° of room outward and 6.4° inward.
- **Fix:** the view looks at the middle of the image, halfway between its edges (`ImageMiddle`, applied
  in `ViewToEye`), and is the image's mean half-angle over the zoom on each side. At zoom 1.25: Pimax
  86.9° wide, 2.26° outward, OpenXR 87.5° wide, 2.5° outward, each with 6.37° of room on both sides.
  The inner side keeps the room it had, and Pimax's outer side has 0.5° less.
- **Test:** the e2e view-width check (`vr_mirror\e2e\README.md`, "Measures"), proven to fail on the build
  before (`reference\e2e_results.md`, "Proven to fail"). The test was written after the fix, against
  the dev skill's order (#7).
- **The horizon while looking up or down.** The smoothing levels the head's view, but the screen looks
  along the head turned by the eye's cant and the aim. A camera turned sideways from the head tilts as
  the head pitches, by about the turn times the sine of the pitch. The first full e2e run with the aim
  found it: `openxr_black_borders` tilted 2.01–2.11° at every smoothing (the fake's 1° cant plus the
  2.5° aim), against 0.64–0.78° before.
  - Pimax had it all along from its 2° cant: `black_borders` 1.22° on frames pitched over 15° on
    `main`.
  - Fixed by levelling the camera itself while stabilization is on (`ViewToEye`). With Smoothing at 0
    the view keeps the headset's roll.
  - Pitched over 15°: Pimax 1.21° → 0.31°, OpenXR 0.64° → 0.11°. This time the test (the tilt
    looking up or down, limit 0.5°) was written and seen to fail before the fix.
  - Left as it was: Pimax's worst tilt at level pitch in `black_borders`, 1.1° on `main` and with the
    fix, during the motion's fast head tilts. OpenXR's exact poses don't show it, so the Pimax frames'
    pose error is the likely cause. Not looked into.
- **Not measured again:** the zoom limits from the real-pose study (`findings\mirror_app.md`, "Core's
  pipeline on the same poses": nothing reaches the edge at 1.29, one frame in 750–1,300 planned at
  1.2). They were measured with the outer side's extra 0.5°.
- **Not known:** whether LMU's OpenXR views carry the eye's turn. The fix doesn't depend on it, because
  the image's middle is the same either way.

## Picture room

### Inside the image actually shown, on ticks too

- The stop and Instant's room are the shown image's alone. On ticks the followed image (the tick's own
  moment) is never shown.
- The look-ahead plans against the images that will be shown at its moments, found from the frames'
  timing (`Pipeline::ShownLead`).
- On ticks at zoom 1.2 (e2e, fixed delay):

  | Measure | Before | After |
  |---|---|---|
  | Stops, Smooth / Smoothest | 13 / 12% | 0% |
  | Jitter as a share of the head's, Smooth / Smoothest | 1.52 / 1.58 | 0.51–0.60 / 0.51–0.53 |
  | Instant's stops | 33% | 27% |

- Drawing once per game frame stayed byte for byte the same.
- Some offline jitter numbers got worse: the first second, and single outputs at fast reversals
  (`findings\e2e.md`, "The edges in the smoothing", with what was tried and dropped).

### One place for the edges

- **Before,** the edges were stage 5: this room service, plus a last-resort limit that the glue ran
  after the smoothing, in two clips:
  - at 95% of the room against the followed image. It was counted, and its pull reached Instant with
    its next frame.
  - then against the shown image, when an older one stands in. It was not counted until that day, and
    had no pull.
- **Since,** both run inside each smoothing, in the same order and with the same numbers. Instant pulls
  its filter along in the same frame instead of the next.
- `stage_replay --golden` gave every view, pose and warp of every run byte for byte the same as before.
  Only the count changed, on the 14 runs drawn on OBS ticks, where it now includes the stops against
  the shown image.
- The experimental second implementation, `ShownPictureEdges`, went with the stage. It took the room
  and the limit against the shown image only, and planned the path against the images shown at its
  moments (`Pipeline::ShownLead`). Its idea, made sound, became the default the same day (above).

### Earlier measurements against the shown image

- **With the fixed delay's ticks, the limit acts far more often against the shown image.** Offline,
  `stage_replay --pipeline --ticks 90 --zoom 1.2 --input edge_test+heartbeat`:

  | Frame picker | Smoothing | Limit with the planned edges / against the shown image | Planned |
  |---|---|---|---|
  | Fixed delay | Smoothest | 0.10% / 6.87% of ticks | 23% / 94% of ticks |
  | Fixed delay | Instant | 16.0% / 24.8% | |
  | Slot timeline | Smoothest | 0.19% / 0.19% | |
  | Slot timeline | Instant | 1.41% / 0% | |

  The fixed delay's tick moment runs about 22 ms before the shown image's slot ("Stage 3: frame
  picker", above). So the view trails the shown image by that much more head motion than the planning
  saw. These counts left out the clip against the shown image (above). e2e: `findings\e2e.md`,
  "Drawing on ticks".
- **On ticks, the clip against the shown image is what shook the view at low zoom** (e2e, the mirror
  app with `output_clock=ticks`).
  - It is a hard clip, and the planning never saw that image. On ticks the path's moments fall between
    slots, so the planning checked the measured head, while the shown image is a frame's prediction,
    16 ms ahead with the fixed delay.
  - Low-zoom jitter as a share of the head's at Smoothest: 1.58 with the fixed delay, 0.98 with the
    slot timeline, against 0.39 for the window drawing per frame.
  - **Tried and dropped: switching the clip off.** With the slot timeline, jitter went 0.98 → 0.20 at
    Smoothest and 1.07 → 0.54 at Smooth, but 0.98% of frames were black. Offline with the fixed delay:
    15%.
- **`ShownPictureEdges` removed the hidden clip, but failed low zoom on ticks too** (the same runs,
  `findings\e2e.md`, "Drawing on ticks").
  - With the slot timeline, the limit acted on 0.66–0.74% at Smoothest. 7 of 8 times were on the
    ticks right after the copy delay is fixed: the image jumps 3 frames, the moments ease over 0.3 s,
    and the averaged lead lags about 1 s.
  - At Smooth the jitter was 1.89–1.98 (limit 1.2), from planning against the runtime's predictions,
    which overshoot on the fast reversal.
  - With the fixed delay, the planning acted on nearly every tick and the limit on 0–1.19%.
- **The stand-ins' lead by comparing poses, not the drawn slot** (`ShownPictureEdges`).
  - Before, the lead was the shown frame's drawn slot (its lag applied) minus the moment. That needed
    the drawn pose stage's lag outside its stage.
  - Then it was where the stand-in's pose sits among the drawn poses around the moment, updated only
    while the drawn poses move at least 0.02° a display period.
  - `stage_replay --pipeline --ticks 60 --zoom 1.2 --edges shown_picture --input
    edge_test+heartbeat`, drawn slot → poses:

  | Frame picker, smoothing | Jitter still | Jitter small | Edge limit | Planned |
  |---|---|---|---|---|
  | Fixed delay, Smooth | 0.0422 → 0.0432° | 0.1621 → 0.1609° | 5.95 → 5.66% | 79.3 → 85.2% |
  | Fixed delay, Smoothest | 0.0421 → 0.0433° | 0.1501 → 0.1496° | 6.24 → 6.39% | 85.3 → 85.3% |
  | Slot timeline, Smooth | 0.0044 → 0.0044° | 0.0041 → 0.0041° | 1.45 → 1.31% | 29.0 → 28.7% |
  | Slot timeline, Smoothest | 0.0050 → 0.0048° | 0.0052 → 0.0052° | 1.45 → 1.31% | 28.3 → 27.7% |

  - Without the 0.02° threshold, the lead followed the predictions' noise while the head was nearly
    still (slot timeline, Smoothest: still jitter 0.025°).
  - At 0.1° it lagged in slow turns (fixed delay, Smoothest: small-move jitter 0.160°).

### Other findings

- **Edges planned for with the look-ahead** (`findings\e2e.md`): `black_borders` at Smoothest, zoom
  1.2, went from jitter 1.06 of the head's and the limit on 1.6% of frames to 0.48 and 0%. Tried and
  dropped there: a correction shaped after the solve, and weight 100,000 with five passes.
- **Before the planning** (`findings\e2e.md`, "The first mirror app"): auto zoom and pinning the view to
  "head + room" near the edge were dropped. Instant's speed-scaled catch-up was kept.
- **A service, not a protocol.** Before, the edges planned the look-ahead's path
  themselves, in about fifteen calls that the glue interleaved with the smoothing's. So a new smoothing
  had to implement the edges' planning protocol, and the reverse. Since, the planning is the
  look-ahead's own (the same margin, weight and passes), and the picture room answers room questions.
  `stage_replay --golden` was byte for byte the same.

## Observer

- In older builds each host assembled and formatted its own trace rows and stamp: the OBS plugin per
  tick, the mirror app in three places, with different columns. A stage's method details (lags,
  corrections) had to be passed out to them.
- Older traces couldn't show:
  - which frame the view followed
  - when the edge limit acted on a repeated frame. It wasn't counted at all.
  - what the drawn pose stage decided per frame

  An analysis of a real recording had to rebuild these from the poses, by guessing (the data folder's
  shake in recordings).

## Removed: the measured drawn pose and the slot-timeline frame picker

Both went with the settings and the experimental output that reached them.
- **The measured drawn pose** (`MeasuredDrawnPose`, the setting `predicted_pose` off): the measured
  head at the frame's de-jittered arrival plus `latency_ms`. It was never exactly the pose a frame was
  drawn with. The OBS plugin always used the pose the game rendered with.
- **The slot-timeline frame picker** (`SlotTimelinePicker`, only with the window's experimental
  `output_clock=ticks` and `frame_picker=slot_timeline`). It kept the fixed delay's picks, but moved
  each tick's moment on by the frames' lead from arrival to display slot, averaged over about 2 s. So
  the view was for the moment the image shows, rather than about 22 ms before it. It never became a
  default, because drawing on ticks lost (`findings\e2e.md`, "Drawing on ticks").
