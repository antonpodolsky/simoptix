# Findings: the mirror app and the smoothing studies

Terms: `reference\terms.md`. What the mirror app is: `vr_mirror\mirror_app\README.md`.

Until 2026-11-07 the mirror app was also a tuning tool, with keys for every setting of the pipeline.
It also held the offline smoothing simulations that the studies below were made with
(`sim_filter.cpp`, `sim_lookahead*.cpp`, `sim_lookahead_*.h`). That code was removed on that date.

In the studies, **"today"** means Instant's filter as it was then, and as it still is: the
`OrientationFilter` with a 0.5 Hz min cutoff, beta 2 and a 0.5° dead zone. The look-ahead smoothing
(Smooth and Smoothest) came out of these studies.

## The mirror app

- **Tried and dropped: Pimax's *predicted* pose per display slot,** in place of the measured head pose
  at each frame's arrival (the first mirror app, before the drawn pose stage). It caused periodic
  stutter, and overshoot on fast head movements in LMU.
- **Why paced presents.** OBS grabs the newest present on its own 90 Hz clock, which drifts against
  the headset's by about 1.1 ms per 100 s. So the two line up roughly every 17 minutes.
  - With presents wobbling around OBS's grab time, OBS repeated and skipped frames for minutes at a
    time. Measured on 2026-10-18: 13.8 repeats per second of motion at one phase and 0.9 at another,
    while the mirror app itself missed 3 frames in 73 s.
  - Steady presents shrink each crossing to a single repeat. They are spaced about 0.13 ms rms in e2e,
    against about 1 ms when presenting right away. About ±2 ms was measured in LMU.
- **Zoom 1.2** left only about 5° of room in the first mirror app. That was too little for noticeable
  smoothing with Instant (tested in the headset). 1.5 left about 11.5° left and right.
- **Low beta** made the view lag further, and the edge catch-up kick in noticeably: a sharp speed-up
  after the start of a turn. Beta 2.0 was chosen by feel in the headset.
- **Window capture** delivered the window at about 55 fps on a 164 Hz monitor, while the mirror app
  showed 90. So a 60 fps recording repeated roughly 1 frame in 10 during head motion. OBS's Game
  Capture on the mirror app hooks its own presents and avoids this. It was used for the 90 fps recordings measured on
  2026-10-18.

## Study 1: look-ahead with a short delay (offline, 2026-10-26)

**The question.** The output is a recording, so the image may run a fixed delay D behind it. Up to
about 150 ms seemed free, because the game's audio already trails the image by about 120 ms. Then the
view shown with a frame can use head poses from up to D after it. How much room, and so zoom, would
that save at today's smoothness? Measured offline on real head poses. Nothing in `vr_mirror\core` or
either host changed.

**The answer:** a forward-backward pass of today's filter over D = 11 frames (122 ms).
- **With exactly today's smoothing,** the 99th-percentile yaw lag falls from 4.6° to 2.6°. At the
  margin zoom 1.29 keeps today, that is zoom 1.16 instead of 1.29: a native width of 2143 instead of
  1926 px (+11%), at Render Quality 1.0.
- **With today's jitter and a little more 3–10 Hz wobble** (min cutoff 1 Hz instead of 0.5, wobble
  0.30 instead of 0.20 px rms while the head shakes): 1.5°, zoom 1.09, 2278 px (+18%).
- **Costs:** 11 frames held, 230 MB of video memory at Render Quality 1.0 (359 MB at 1.25).
- **Audio:** 122 ms matches the audio's measured lag. So the −120 ms sync offset on the game audio
  could go back to about 0.
- **Roll lock:** keeping the horizon level needs zoom 1.10 on its own for the whole frame (head roll
  p99 10–12°). With the roll lock on, the whole frame can't go below about 1.10, however little the
  view lags.
- Below about 60 ms of delay the gain is small (zoom 1.22–1.25).
- The other look-ahead smoothers are worse. "Causal, D ahead" leads the head by up to 30° at the start
  of fast flicks. The Gaussian can't match today's jitter below 144 ms, and lets through 3.6× today's
  wobble.

### How it was measured

**The program.** `sim_lookahead.cpp` (with `sim_lookahead_poses.h`, `_smoothers.h`, `_measures.h`), a
standalone program like `sim_filter.cpp`. It was built in the mirror app's folder with
`cl /std:c++17 /EHsc /O2 /W3 /DNOMINMAX /I..\core sim_lookahead.cpp @..\core\build\pipeline.rsp`, and
run as `sim_lookahead.exe [--predicted] [--diag] [--csv out.csv] trace_pimax.csv ...`. Given a zoom
(`ZOOM`), it ran core's pipeline instead (`vr_mirror\core\pipeline`, built by `pipeline_builder`,
through a `PoseSource` of the recorded poses): "Core's pipeline on the same poses", below. Its
outputs, the extracted traces' paths and the helper scripts are in the data folder's `lookahead_sim\`.

**Head poses.** The `P` rows (the measured head pose, about 730 Hz) of the four OBS plugin frame
traces in the data folder's `2026-10 late frames after drops\trace\`, slerped onto the 90 Hz display
grid. That is 4.3 h with game frames arriving:
- 2026-10-21, a race at Imola, 76 min
- 2026-10-21, an evening at Monza, 73 min
- 2026-10-22 at Monza, 8 min
- 2026-10-23, a race at Monza, 98 min

They were split at gaps and at a view recenter. Left out: the first second of each stretch, and 140 s
of the headset in the hands (taking it off and putting it down: rolls to 76°, a 35° recenter). Checked
against the `S` rows (the runtime's predicted pose per display slot, which the plugin filtered by
default) and the four e2e replays of the time.

**Parts of a session,** from the head alone over ±0.5 s:
- still: slower than 3°/s throughout (12% of the time)
- vibration: no turn faster than 20°/s, and the head shaking by at least 0.01° on the jitter measure
  (16%). The name is the simulation's label only. On a sim rig nothing shakes the driver, so this is
  the driver's own small, quick head movement and the headset's tracking noise. The cause wasn't
  measured.
- other movement: the rest (72%)

**Smoothers,** each roll-locked like core's pipeline:
- today: `OrientationFilter` with the defaults (min cutoff 0.5 Hz, beta 2, dead zone 0.5°, max lag
  20°). The catch-up and the last-resort edge limit of core's pipeline (`vr_mirror\core\pipeline\`)
  are left out, for every smoother. This measures what each one needs, not what the edge forces.
- causal, D ahead: today's filter, showing its output from D after the frame
- forward-backward: the same filter run backwards over the forward output, from D ahead down to the
  frame, started afresh for every frame
- Gaussian: a weighted mean of the rotation vectors relative to the frame's head pose, over ±D

**Measures,** on the view's direction:
- jitter: e2e's "smoothed view jitter", what's left after a quadratic fit over ±50 ms. It sees mostly
  fast shake. Of a sine it leaves 10% at 8 Hz, 39% at 12 Hz, and all from 20 Hz.
- wobble: the same over ±200 ms (12% at 2 Hz, 45% at 3 Hz, all from 4 Hz). So it is mostly the 3–10 Hz
  motion that the jitter misses: small, quick head movement that isn't a deliberate look.
- ratio: the view's figure divided by the head's, on the same frames
- lag: where the view's centre sits in the head's image, horizontally (yaw) and vertically (pitch),
  over all frames used. A view ahead of the head counts the same as one behind.

**Equal smoothness:** jitter on the still and the vibration parts no higher than today's, within 2% or
0.0001°, whichever is larger. For each smoother and D, the parameters with the least p99 yaw lag were
picked (cutoff 0.5–4 Hz, beta 0–12, sigma 3–100 ms). Table A keeps today's wobble too.

**Zoom,** for the Dream Air's left eye (tangents L 1.195, R 1.173, up and down 0.984), a 16:9 output,
and the 0.995 safety of `Fits`:
- zoom for p99 yaw lag: the lowest zoom whose horizontal room, on the side the view trails, covers 99%
  of frames
- today's margin: zoom 1.29 gives 7.13° on the tighter side, 1.56 times today's p99 lag. The same
  factor is applied to each smoother.
- whole frame: the lowest zoom at which 99% of frames fit entirely (yaw, pitch and the roll lock's
  roll), as the pipeline's catch-up judges it
- native width: 2 × 1.173 / zoom × 1059 px (1323 at Render Quality 1.25)
- video memory: D frames × 20.9 MB (32.6 MB at Render Quality 1.25)

### Results

Degrees, and ratios against the head's own figure. At native width one degree at the centre is
18.5 px.

**Table A: today's smoothing** (today's parameters, delayed, with jitter and wobble as today):

| Smoother | D, frames | Parameters | Jitter, vibration (ratio) | Wobble, still / vibration (ratio) | Yaw lag p95 / p99 / max | Pitch lag p95 / p99 / max | Zoom for p99 yaw lag | Native width RQ 1.0 / 1.25 | Zoom at today's margin (width) | Zoom, whole frame (p99) | VRAM for D, RQ 1.0 / 1.25 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| none (roll lock only) | 0 | - | 0.0166 (1.000) | 0.0331 (1.000) / 0.1869 (1.000) | 0.00 / 0.00 / 0.0 | 0.00 / 0.00 / 0.0 | 1.005 | 2472 / 3088 | 1.005 (2472) | 1.096 | - |
| today (causal) | 0 | 0.5 Hz, beta 2 | 0.0003 (0.017) | 0.0004 (0.012) / 0.0107 (0.057) | 2.77 / 4.58 / 16.0 | 1.65 / 2.69 / 13.3 | 1.171 | 2122 / 2651 | 1.290 (1926) | 1.215 | - |
| forward-backward | 3 (33 ms) | 0.5 Hz, beta 2 | 0.0003 (0.017) | 0.0004 (0.012) / 0.0106 (0.057) | 2.52 / 3.99 / 8.5 | 1.52 / 2.44 / 8.1 | 1.148 | 2165 / 2704 | 1.249 (1989) | 1.191 | 63 / 98 MB |
| forward-backward | 5 (56 ms) | 0.5 Hz, beta 2 | 0.0003 (0.017) | 0.0004 (0.012) / 0.0106 (0.057) | 2.36 / 3.60 / 6.2 | 1.44 / 2.28 / 5.9 | 1.133 | 2194 / 2741 | 1.223 (2031) | 1.175 | 104 / 163 MB |
| forward-backward | 9 (100 ms) | 0.5 Hz, beta 2 | 0.0003 (0.016) | 0.0004 (0.012) / 0.0103 (0.055) | 2.03 / 2.91 / 5.2 | 1.27 / 1.97 / 4.1 | 1.106 | 2246 / 2806 | 1.178 (2109) | 1.150 | 188 / 293 MB |
| forward-backward | 11 (122 ms) | 0.5 Hz, beta 2 | 0.0003 (0.016) | 0.0004 (0.012) / 0.0100 (0.053) | 1.88 / 2.62 / 6.3 | 1.19 / 1.83 / 4.7 | 1.095 | 2268 / 2833 | 1.159 (2143) | 1.141 | 230 / 359 MB |
| forward-backward | 13 (144 ms) | 0.5 Hz, beta 2 | 0.0003 (0.015) | 0.0004 (0.012) / 0.0096 (0.051) | 1.73 / 2.37 / 6.8 | 1.12 / 1.71 / 5.7 | 1.086 | 2287 / 2857 | 1.144 (2172) | 1.133 | 272 / 424 MB |
| causal, D ahead | 9 (100 ms) | 0.5 Hz, beta 2 | 0.0003 (0.017) | 0.0004 (0.012) / 0.0106 (0.057) | 2.01 / 2.86 / 17.3 | 1.26 / 1.96 / 5.1 | 1.104 | 2250 / 2811 | 1.175 (2115) | 1.149 | 188 / 293 MB |
| causal, D ahead | 11 (122 ms) | 0.5 Hz, beta 2 | 0.0003 (0.017) | 0.0004 (0.012) / 0.0106 (0.057) | 1.85 / 2.54 / 23.9 | 1.18 / 1.81 / 8.3 | 1.092 | 2274 / 2841 | 1.154 (2152) | 1.138 | 230 / 359 MB |
| causal, D ahead | 13 (144 ms) | 0.5 Hz, beta 2 | 0.0003 (0.017) | 0.0004 (0.012) / 0.0106 (0.057) | 1.68 / 2.28 / 29.7 | 1.10 / 1.68 / 11.4 | 1.083 | 2295 / 2867 | 1.138 (2183) | 1.130 | 272 / 424 MB |

**Table B: today's jitter** (wobble may be higher, parameters tuned per D):

| Smoother | D, frames | Parameters | Jitter, vibration (ratio) | Wobble, still / vibration (ratio) | Yaw lag p95 / p99 / max | Pitch lag p95 / p99 / max | Zoom for p99 yaw lag | Native width RQ 1.0 / 1.25 | Zoom at today's margin (width) | Zoom, whole frame (p99) | VRAM for D, RQ 1.0 / 1.25 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| causal (no delay) | 0 | 1 Hz, beta 1.5 | 0.0003 (0.020) | 0.0011 (0.032) / 0.0164 (0.088) | 2.10 / 3.85 / 16.9 | 1.23 / 2.11 / 13.7 | 1.142 | 2176 / 2719 | 1.240 (2004) | 1.188 | - |
| forward-backward | 3 (33 ms) | 1 Hz, beta 2 | 0.0004 (0.023) | 0.0011 (0.032) / 0.0188 (0.101) | 1.69 / 2.87 / 7.2 | 1.02 / 1.70 / 6.7 | 1.104 | 2250 / 2811 | 1.175 (2114) | 1.151 | 63 / 98 MB |
| forward-backward | 5 (56 ms) | 1 Hz, beta 2 | 0.0004 (0.023) | 0.0011 (0.032) / 0.0186 (0.100) | 1.50 / 2.46 / 4.6 | 0.92 / 1.51 / 4.5 | 1.089 | 2282 / 2851 | 1.149 (2161) | 1.137 | 104 / 163 MB |
| forward-backward | 9 (100 ms) | 0.7 Hz, beta 3 | 0.0004 (0.023) | 0.0007 (0.020) / 0.0172 (0.092) | 1.28 / 1.71 / 4.4 | 0.86 / 1.30 / 3.6 | 1.062 | 2338 / 2921 | 1.104 (2251) | 1.117 | 188 / 293 MB |
| forward-backward | 11 (122 ms) | 1 Hz, beta 2 | 0.0003 (0.019) | 0.0010 (0.031) / 0.0163 (0.087) | 1.03 / 1.50 / 5.4 | 0.67 / 1.06 / 4.2 | 1.055 | 2356 / 2943 | 1.091 (2278) | 1.111 | 230 / 359 MB |
| forward-backward | 13 (144 ms) | 1 Hz, beta 2 | 0.0003 (0.018) | 0.0010 (0.031) / 0.0150 (0.080) | 0.90 / 1.30 / 5.6 | 0.61 / 0.96 / 5.0 | 1.048 | 2372 / 2963 | 1.079 (2302) | 1.107 | 272 / 424 MB |
| causal, D ahead | 9 (100 ms) | 1 Hz, beta 2 | 0.0004 (0.023) | 0.0011 (0.032) / 0.0188 (0.101) | 1.12 / 1.62 / 18.1 | 0.72 / 1.14 / 5.9 | 1.059 | 2346 / 2931 | 1.098 (2262) | 1.114 | 188 / 293 MB |
| causal, D ahead | 11 (122 ms) | 1 Hz, beta 2 | 0.0004 (0.023) | 0.0011 (0.032) / 0.0188 (0.101) | 0.94 / 1.31 / 24.7 | 0.63 / 0.99 / 9.2 | 1.048 | 2370 / 2961 | 1.080 (2301) | 1.109 | 230 / 359 MB |
| causal, D ahead | 13 (144 ms) | 1 Hz, beta 2 | 0.0004 (0.023) | 0.0011 (0.032) / 0.0188 (0.101) | 0.78 / 1.09 / 30.5 | 0.56 / 0.86 / 12.2 | 1.041 | 2387 / 2982 | 1.067 (2328) | 1.106 | 272 / 424 MB |
| Gaussian | 13 (144 ms) | sigma 70 ms | 0.0003 (0.019) | 0.0064 (0.193) / 0.0382 (0.204) | 0.20 / 0.36 / 3.9 | 0.19 / 0.34 / 2.5 | 1.015 | 2447 / 3057 | 1.025 (2423) | 1.097 | 272 / 424 MB |
| Gaussian, smoothest it gets (not as smooth as today) | 9 (100 ms) | sigma 70 ms | 0.0008 (0.046) | 0.0093 (0.280) / 0.0535 (0.286) | 0.17 / 0.30 / 3.1 | 0.16 / 0.28 / 1.9 | 1.014 | 2451 / 3062 | 1.022 (2431) | 1.097 | 188 / 293 MB |
| Gaussian, smoothest it gets (not as smooth as today) | 5 (56 ms) | sigma 50 ms | 0.0022 (0.132) | 0.0201 (0.607) / 0.1089 (0.583) | 0.09 / 0.17 / 1.4 | 0.09 / 0.16 / 1.0 | 1.010 | 2461 / 3074 | 1.014 (2450) | 1.096 | 104 / 163 MB |

On the still part, every row of both tables except the last two leaves under 0.0001° of jitter (today
0.00001°, the head 0.0021°).

### What the numbers say

- **Today's filter already removes nearly all jitter.** 0.0003° is left while the head shakes: 1.7% of
  the head's, about 0.006 px. So equal jitter is far below anything visible.
- **Where the smoothers differ is the 3–10 Hz wobble.** While the head shakes: the head's 0.19°
  (3.5 px), today's 0.011° (0.20 px), the Gaussian's at 144 ms 0.038° (0.7 px).
- **With today's smoothing, each 100 ms of delay cuts the p99 yaw lag by about 1.5°:** 4.58, 3.99,
  3.60, 2.91, 2.62 and 2.37° at 0, 33, 56, 100, 122 and 144 ms.
- **Part of the gain at equal jitter needs no delay at all.** Today's filter at 1 Hz and beta 1.5 has
  today's jitter and a p99 of 3.85° (zoom 1.24 at today's margin). It costs 1.5× today's wobble while
  the head shakes, and 2.8× with the head still. The delay adds the rest: forward-backward at 122 ms
  and 1 Hz gives 1.50°.
- **"Causal, D ahead" leads the head in fast flicks.** Its p99 is like forward-backward's. But at the
  start of a 300°/s flick (the 2026-10-21 race, 2666.2 s on the trace's clock), it ran 24° (122 ms) to
  30° (144 ms) ahead of the head.
  - Forward-backward stays within 5–7° there, because its backward pass follows quickly once the head
    moves fast.
  - Today's own maximum, 16° behind, is the same flick.
- **The Gaussian has the least lag** (p99 0.36° at 144 ms). But it can't reach today's jitter below
  144 ms, and lets through 3.6× today's wobble while the head shakes, and 16× when the head is still.
- **The roll lock costs room of its own.** Head roll p99 is 10–12° (leaning in corners), and levelling
  that needs zoom 1.10 for the whole frame with no smoothing at all. Whole frame: today 1.215,
  forward-backward at 122 ms 1.141 (today's smoothing) or 1.111 (1 Hz).
- **Pitch never limits.** p99 pitch lag is at most 2.7°, against 17° of vertical room at 1.29.
- **The same on other inputs:**
  - on the runtime's predicted poses (the `S` rows): today 4.57°, forward-backward at 122 ms 2.61°
    (4.58° and 2.62° on measured poses)
  - per session: today 4.43–4.86°, forward-backward at 144 ms and 1 Hz 1.26–1.34°
  - on the four e2e replays (12 s each, no still part): today 4.82°, forward-backward at 122 ms 2.83°

### Core's pipeline on the same poses (2026-10-30)

`sim_lookahead.exe` given a zoom (`ZOOM`) ran core's pipeline itself (`vr_mirror\core\pipeline`):
Instant's catch-up, the look-ahead's edge planning and the last-resort limit included, with LMU's eye
and its 2° cant. Input: the four traces' measured poses, counted in play (game frames arriving, the
headset not handled), 1.38 million frames. Jitter is as a share of the head's, as in the tables above.

| Zoom | Smoothing | lag p99 | lag max | jitter still | jitter, small moves | clamped | planned |
|---|---|---|---|---|---|---|---|
| 1.29 | Instant | 3.29° | 5.98° | 0.004 | 0.029 | 0.000% (3 frames) | – |
| | Smooth | 0.95° | 2.56° | 0.010 | 0.016 | 0% | 0% |
| | Smoothest | 0.75° | 2.39° | 0.009 | 0.005 | 0% | 0% |
| 1.2 | Instant | 2.60° | 4.92° | 0.004 | 0.061 | 0.019% | – |
| | Smooth | 0.95° | 2.56° | 0.010 | 0.017 | 0.002% | 0.13% |
| | Smoothest | 0.75° | 2.39° | 0.009 | 0.005 | 0.001% | 0.08% |
| 1.1 | Instant | 1.60° | 2.92° | 0.011 | 0.229 | 2.05% | – |
| | Smooth | 0.93° | 2.35° | 0.185 | 0.548 | 1.81% | 14.6% |
| | Smoothest | 0.73° | 2.30° | 0.060 | 0.281 | 1.68% | 11.8% |

- At 1.29 nothing reaches the edge in play. Out of play (no game frames, the headset handled), the
  last-resort limit acts on 0.06% of frames at every smoothing.
- At 1.2 the edge planning acts on one frame in 750–1,300, and the limit almost never. The look-ahead
  stays as calm as at 1.29.
- At 1.1 the room is too small. The planning acts on 12–15% of frames, and Smooth and Smoothest get
  shakier than Instant.
- So about 1.2 is the lowest zoom worth using with the look-ahead.

### Not covered

- The pipeline's catch-up and edge limit in Tables A and B, which measure what each smoother needs,
  not what the edge forces. The real pipeline's numbers are in the section before.
- The eye's 2° cant, and the right eye.
- How it looks. There was no recording or headset check. Whether 0.016° of wobble instead of 0.011°
  shows is for a recording to tell.
- A roll lock that lets the horizon follow slow leans. Its room cost is the same for every smoother
  here.
- The delay buffer itself: a copy per frame, and GPU time. The video memory figures are frame counts
  times the given frame sizes, not measured. Late and dropped frames inside the buffer have been
  covered by e2e at every smoothing since (`findings\e2e.md`, "Smoothing compared with Instant: what
  it found").
- One driver, LMU at Imola and Monza, four sessions.

### Tried and dropped

- **Forward-backward with the backward pass seeded** where an endless backward pass would be: ahead
  of the forward output by the filter's delay at its velocity.
  - It had the least lag of the filter variants (p99 1.04° at 33 ms).
  - But it left 0.0072° of jitter while the head shakes: 43% of the head's, against today's 1.7%.
  - At min cutoff the filter's delay is 0.6 s. Extrapolating the forward output's velocity over that
    turns its frame-to-frame noise into jitter.
- **The plain forward-backward pass is nearly "causal, D ahead"** while the head is slow. Started
  afresh, Instant's filter (`OrientationFilter`) barely moves within D, because its speed estimate
  takes about 160 ms to rise and its cutoff stays near min cutoff. Its view lies between today's and
  the one D ahead, within 0.1° of the latter in p99, but without the leads in flicks.

## Study 2: other smoothing methods (offline, 2026-10-26)

**The question,** before designing the look-ahead smoothing and its edge handling: how do other ways of
smoothing with future samples, of levelling the horizon, and of handling the zoom do on the recorded
head poses? Each method is given only what a live output delayed by D can see.

### The methods

- **Velocity-adaptive filter:** a first-order filter whose time constant falls with the turning
  speed, linearly from 1 s at rest to 0.1 s at about 250°/s and above.
  - Its speed estimate is itself smoothed forward and backward, so it doesn't trail the head. Today's
    filter takes about 160 ms to see a turn begin.
  - The filter runs forward, then backward over its own output.
  - A second pass that speeds the smoothing up only where the first one drifted furthest from the head
    needs the whole recording, and is left out.
- **Jerk path:** solves for the path at once. It stays close to the head, and penalises the path's
  jerk with a time constant τ.
  - Its frequency response is 1 / (1 + (ωτ)⁶): steep, and with no delay.
  - A path at constant angular velocity, or constant angular acceleration, costs nothing. So **it
    follows a steady turn exactly, with no lag at all**.
  - The crop can be part of the same optimisation (the path kept inside the image as a constraint),
    instead of a correction afterwards.
- **Horizon lock:** replaces the roll entirely or partly (a blend between the locked and the unlocked
  view), or follows only the head's slow roll.
- **Zoom:** a zoom that changes over time (the tightest any frame in a window needs, smoothed) was not
  taken up. VR Mirror keeps a fixed zoom on purpose. An earlier automatic zoom changed it during turns,
  and the e2e tests guard against that.

### On the recorded head poses

The same simulation, input and measures as Study 1 (4.3 h of measured head poses), run with
`sim_lookahead.exe --methods`. The methods were in `sim_lookahead_methods.h`.

- **Jerk path:** over a window of up to 6τ before the frame and D after it. On the rotation vectors
  relative to the frame's head pose, that is a fixed linear filter, so each window size has fixed
  weights. Checked exact on constant velocity and acceleration.
- **Velocity-adaptive:** the speed taken from the D frames ahead too, a forward pass, then backward
  over D. The second pass needs the whole clip and is left out.
- **Parameters searched:** jerk path τ 10–800 ms. Velocity-adaptive time constant 0.5–2 s at rest,
  20–100 ms at speed, full speed at 30–500°/s. Picked as in Study 1: today's jitter on the still and
  vibration parts (within 2% or 0.0001°), then the least p99 yaw lag.

| Smoother | D | Parameters | Jitter, vibration (× today) | Wobble, still / vibration (× today) | Yaw lag p95 / p99 / max | Zoom at today's margin (width RQ 1.0 / 1.25) |
|---|---|---|---|---|---|---|
| today (causal) | 0 | 0.5 Hz, beta 2 | 0.0003° (1.0) | 0.0004 / 0.0107° (1.0 / 1.0) | 2.78 / 4.58 / 16.0° | 1.290 (1926 / 2406 px) |
| forward-backward (the study's pick) | 11 (122 ms) | 1 Hz, beta 2 | 0.0003° (1.1) | 0.0010 / 0.0163° (2.5 / 1.5) | 1.03 / 1.50 / 5.4° | 1.091 (2278 / 2846 px) |
| **jerk path** | 13 (144 ms) | τ 100 ms | 0.0003° (1.1) | 0.0031 / 0.0189° (7.8 / 1.8) | 0.27 / 0.50 / 7.8° | 1.033 (2405 / 3004 px) |
| jerk path | 11 (122 ms) | τ 100 ms | 0.0006° (2.2) | 0.0039 / 0.0235° (9.8 / 2.2) | 0.27 / 0.49 / 7.4° | 1.032 (2407 / 3007 px) |
| jerk path | 11 (122 ms) | τ 70 ms | 0.0006° (1.9) | 0.0062 / 0.0385° (16 / 3.6) | 0.19 / 0.34 / 4.8° | 1.024 (2426 / 3031 px) |
| velocity-adaptive | 13 (144 ms) | 0.5 s at rest, 20 ms from 125°/s | 0.0004° (1.3) | 0.0017 / 0.0103° (4.3 / 1.0) | 2.25 / 4.17 / 10.6° | 1.261 (1970 / 2461 px) |
| velocity-adaptive | 0 | 1 s at rest, 100 ms from 60°/s | 0.0004° (1.3) | 0.0012 / 0.0097° (3.0 / 0.9) | 4.31 / 7.15 / 22.0° | 1.486 (1672 / 2089 px) |

At native width one degree at the centre is 18.5 px. So 0.0189° of wobble is 0.35 px, and today's
0.0107° is 0.20 px. The runtime's predicted poses (`--predicted`, the `S` rows the plugin filtered)
give the same within 0.01°.

**Horizon locks.** The zoom at which 99% of frames fit whole (so roll counts), with no smoothing and
then with today's, and the horizon's tilt in the view (p99):

| Horizon | Zoom, whole frame, no smoothing / today | Horizon tilt in the view |
|---|---|---|
| level (today) | 1.096 / 1.215 | 0° |
| keeps 25% of the head's roll | 1.077 / 1.204 | 2.8° |
| keeps 50% | 1.056 / 1.195 | 5.6° |
| keeps all of it (no lock) | 1.005 / 1.184 | 11.2° |
| follows the head's roll below 0.1 Hz (slow leans through) | 1.094 / 1.207 | 7.9° |
| follows below 0.02 / 0.2 Hz | 1.099 / 1.090 (no smoothing) | 5.3° / 9.0° |

### Longer delays: when the recording may run further behind

The stabilization still runs live, in OBS while recording. Only the recorded image runs a fixed delay
behind, with the G-meter and audio delayed to match. A recording doesn't mind a longer delay, so the
jerk path was also run at 0.2, 0.3, 0.5 and 1 s (`--methods`, τ up to 800 ms). Ratios are against
today's filter, and px at native width.

| Delay | τ | Yaw lag p99 / max | Small movement left while not turning (3–10 Hz) | Fast shake left | Head held still | VRAM, RQ 1.0 / 1.25 |
|---|---|---|---|---|---|---|
| 0 (today) | – | 4.58 / 16.0° | 0.20 px | 1× | 0.007 px | – |
| 144 ms | 100 ms | 0.50 / 7.8° | 0.35 px (1.8×) | 1.1× | 0.06 px | 0.27 / 0.42 GB |
| 300 ms | 140 ms | 0.74 / 10.7° | 0.22 px (1.1×) | 1.2× | 0.04 px | 0.56 / 0.88 GB |
| **500 ms** | **100 ms** | **0.56 / 7.1°** | **0.16 px (0.8×)** | **0.5×** | **0.03 px** | **0.94 / 1.47 GB** |
| 500 ms | 280 ms | 1.96 / 22.5° | 0.06 px (0.3×) | 0.3× | 0.01 px | 0.94 / 1.47 GB |
| 1 s | 140 ms | 0.88 / 10.2° | 0.07 px (0.4×) | 0.3× | 0.02 px | 1.9 / 2.9 GB |

- **From about half a second, the jerk path is better than today's filter at everything that shows.**
  It trails the head by a tenth as much, and leaves less of the small movement and of the fast shake.
  - The one higher figure is the remainder with the head held completely still: 0.03 instead of
    0.007 px. Today's filter has a dead zone, and the jerk path none.
  - Both are far below a pixel.
- **A second gains little over half a second,** and doubles the memory. A longer τ trades lag for
  smoothness at any delay. At 500 ms and τ 280 ms: a third of today's small movement, but 2° of lag.
- **The results jump between neighbouring τ,** because of the window's end. At 500 ms, τ 70, 100 and
  140 ms give 1.8×, 0.8× and 0.75× today's small movement. The setting needs checking at the chosen
  delay.
- **Memory:** each frame held is a copy of the game eye. Half a second is 45 of them.

### Tremor and tracking noise: a time constant that follows the head's speed

The jerk path's one weakness was the remainder with the head nearly still: 0.03 px at half a second,
against today's 0.007 px, which has a dead zone. So its time constant was made to follow the head's
speed, as in the velocity-adaptive filter.
- It is long while the head is nearly still, so tremor and tracking noise average away, and short in
  turns.
- A moment's speed is the fastest turn within 0.25 s of it. So the smoothing loosens before a turn that
  the look-ahead sees coming.
- Run with `sim_lookahead.exe --adaptive`. px at native width:

| | Today | Jerk path, 1 s, τ 140 ms | **Smoothest: 1 s, τ 200 ms still (below 5°/s), 50 ms from 100°/s** | Smooth: 0.5 s, τ 300 / 50 ms |
|---|---|---|---|---|
| Head still: fast / slower remainder | 0.0002 / 0.0073 | 0.0003 / 0.016 | **0.0002 / 0.011** | 0.0002 / 0.010 |
| Head moving slightly (the "vibration" part): fast / slower | 0.0053 / 0.197 | 0.0018 / 0.074 | **0.0012 / 0.076** | 0.0044 / 0.136 |
| Fast shake left while turning | 0.0075 | 0.0010 | **0.0018** | 0.0025 |
| Yaw lag p99 / largest | 4.58 / 16.0° | 0.88 / 10.2° | **0.75 / 2.4°** | 0.95 / 2.6° |

- **Tremor and tracking noise no longer show in the view's direction.** Every remainder is below a
  tenth of a pixel. They can still move the head sideways, which shifts near cockpit parts against the
  scenery. Rotation-only stabilization can't correct that.
- **A short time constant in turns** (50 ms, reached at 100°/s) rounds off the start and end of a fast
  turn least. The largest lag falls from 7° to 2.4°.
  - 100 ms from 40°/s had 0.71° p99, but 6.9° at the worst flick. In the e2e test of abrupt synthetic
    turns, the view reached the edge limit (4% of frames) and stalled once.
  - With 50 ms from 100°/s it never reaches it.
- **These became the presets** in `vr_mirror\core\pipeline\4_smoothing\lookahead_smoothing.h`:
  `LookaheadFor(2)` is Smoothest and `LookaheadFor(1)` is Smooth, behind the `smoothing` settings 3
  and 2.
- **Tried and dropped: one time constant per frame.** The first adaptive version chose a single time
  constant for each frame's whole window, from that frame's speed. Neighbouring frames then got
  different smoothings, and the view jumped between them: 30–200× today's jitter while the head moved
  slightly (0.1–0.9 px). Giving every moment its own time constant inside one solve keeps consecutive
  frames on nearly the same path.

### What the numbers say

- **The jerk path is the one to build on.** At 144 ms it has today's jitter and a p99 yaw lag of 0.50°,
  against today's 4.58° and forward-backward's 1.50°.
  - That is zoom 1.03 at today's margin: 2405 px across a native recording instead of 1926 (+25%).
  - Its cost is the 3–10 Hz wobble: 1.8× today's while the head shakes (0.35 px instead of 0.20), and
    more with the head still (0.06 px against 0.01). Whether that shows is for a recording to tell.
- **It needs the delay.** At 122 ms none of its settings reaches today's jitter (the best: 1.9×,
  0.01 px).
  - With the frame near the end of the window it sees, a longer τ acts like an extrapolation and
    amplifies noise. Past τ ≈ 100 ms the jitter rises again, and the best τ depends on D. At 144 ms,
    τ 70 and 140 ms are clearly worse than 100.
  - Smoothing a whole recording afterwards would avoid this. A live output can't.
  - Its largest lag is at the same 300°/s flick as everyone's: 7.8° behind (forward-backward: 5.4°
    ahead).
- **Then the horizon lock sets the zoom.** With the jerk path the whole frame fits at zoom 1.098
  (p99), which is the level horizon alone (1.096). Going lower means letting some roll through.
  Keeping half of it gives 1.056, with the horizon tilting by up to 5.6° (p99). That is a choice of
  look, still to be made.
- **A lock that lets only slow leans through saves nothing:** below 0.1 Hz, 1.094 against 1.096. The
  head's roll in corners lasts seconds, not minutes, so it isn't "slow".
- **The velocity-adaptive filter is worse than today's** at every delay (p99 4.2–7.2°). One
  first-order stage with a long time constant at rest needs much more lag for the same jitter than
  today's two stages with a dead zone. Its look-ahead speed estimate alone doesn't make up for that.
- **For the edge:** the jerk path can take the crop into the optimisation. Then the path bends away
  from an edge only as far, and as early, as that edge needs, which is the edge planning's goal. With
  a window of D frames, it would see an edge coming up to D ahead.

### Not covered

- How it looks: no recording or headset check.
- The velocity-adaptive filter's second pass, and whole-recording versions. A live output can't wait
  for the end.
- The crop constraint itself. Like Study 1, these numbers measure what each smoother needs, without
  the pipeline's catch-up or edge limit.
- The delay buffer: 13 frames are 272 MB of video memory at Render Quality 1.0 (424 MB at 1.25),
  computed.

## Open

- **A covered window.** With the mirror app behind other windows, capture dropped to about 20 fps
  in one early test run. To do: an e2e scenario for it, and opting the mirror app out of Windows power
  throttling if needed. The OBS plugin doesn't need it.
- **Calibrate the e2e fake game.** Find out which head pose LMU really renders each frame with, so the
  on-screen e2e checks can be graded (`findings\e2e.md`, "Open question: the fake game's pose").
