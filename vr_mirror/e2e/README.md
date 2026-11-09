# End-to-end tests

These tests run the **real, unmodified mirror app** (`simoptix_vr_mirror.exe`, `vr_mirror\mirror_app\`,
which uses the same core and frame sources as the OBS plugin). They run it against a simulated Pimax
headset and game, or against a simulated game with the OpenXR layer in it. They record its window the
way OBS does and check what a viewer would see. The tests use no headset, LMU or OBS, and they test the
core and the mirror app, not the OBS plugins.

The local files test (`local_files\`) is separate. It runs the real OBS plugins in a private copy of
OBS, and the real OpenXR layer in a fake game. It checks the files they write under
`%LOCALAPPDATA%\simoptix` ("The local files test", below).

## Terms

| Term | Meaning |
|---|---|
| slot check | A way to find the drawn pose: it judges from thumbnails which display slot a frame was really drawn for |
| cockpit correction | A refinement of the drawn pose. It measures how the fixed cockpit moved between two frames and corrects the pose by what the head poses don't explain |
| mirror app | `simoptix_vr_mirror.exe`, VR Mirror in a desktop window (`vr_mirror\mirror_app\`). The e2e tests run it |
| replay | A real session played back through the fake runtime: its head poses, the runtime's predictions, and every game frame at its real arrival time and with the slot it was really drawn for |
| thumbnail | A 64×32 colour image of the middle of an eye image. The slot check compares them |
| look-ahead | The method behind Smooth and Smoothest. The view runs half a second or a second behind the game, and the smoothing plans a calm path from the head's movement in that time |
| lag | How many display slots earlier than its arrival slot the game drew a frame. 0 is on time. A lag appears while the game is behind |
| PVR | Pimax's own name for its client interface: the client library `libPVRClient64.dll` that Pimax Play installs, and the table of functions it hands out. The project's code that wraps it is named after it |
| display slot | One of the regular moments, 90 per second, at which the headset shows a new image. The game draws each frame for one slot, with the head pose the runtime predicted for that slot |
| client mirror | A feature of the Pimax runtime. It gives other programs a half-size copy of the left eye image the game draws, and a signal for each new frame |
| patch | A small square (32×32 texels) cut from an eye image. The cockpit correction measures how patches move, and the e2e runner reads them from the captured window |
| slot lead | The runtime's next display slot minus the moment a frame arrived. It shifts when the game queues frames |
| observer | The debugging code beside the pipeline. It numbers the stamp and writes the frame trace |
| frame trace | The CSV file that the Debug setting (or the mirror app's `SIMOPTIX_VR_MIRROR_TRACE`) writes, with one row per drawn frame: its poses and timing. With the Pimax source, Debug also writes a second file of head poses, predictions and arrivals. With the OpenXR source, the second file holds the frames the layer published and how the game handed their images over |
| output delay | How far VR Mirror's view runs behind the game: about 1 s with Smoothest, about 0.5 s with Smooth, none with Instant, and 0 while the source is hidden. The G-meter, race auto-record and OBS's audio follow it |
| fake runtime | The stand-in for Pimax's client library (`vr_mirror\e2e\fake_pvr\`) that the e2e tests use in place of a headset and a game |
| drawn pose | The head pose a game frame was drawn with. Stage 2 of the pipeline answers it for each frame, and the code is named after it |
| edge stop | The smoothing's last resort. It holds the view at 95% of its picture room when the smoothing's own planning wasn't enough |
| game behind | The game's GPU has fallen behind and queues up to two frames. Each arrives one or two display slots after the slot it was drawn for |
| timing gate | The part of the slot check that lets a slot be judged only while the frames' timing shows a queue |
| stage replay | `stage_replay.exe`. It runs the pipeline's stages offline, without the mirror app, the fake runtime or the GPU |
| host | A program that runs VR Mirror's pipeline and shows its result: the OBS plugin or the mirror app |
| golden file | The file `stage_replay --golden` writes: every output of the pipeline over a fixed set of inputs, to compare byte for byte |

## The tests

Eight synthetic tests check what synthetic motion tests best:
- the smoothing
- dropped frames
- a still head
- black borders (also at zoom 1.2)
- the cockpit correction on a game that falls behind, and the slot check alone on the same game for
  comparison
- what the mirror app does while no game runs

Six more tests run on the OpenXR layer's frames (below). Every other edge case is a **replay of real
footage** (five of them). A replay holds the real head poses and frame timing of a session, and the
game's real thumbnails as the slot check saw them. Each test has been shown to fail when what it checks
is broken, except the stall measure.

**Every test runs at all three smoothings.** The Instant run keeps the test's own name. The others are
`<name>_smooth` and `<name>_smoothest`. Each run is graded on its own limits. Then the runner sets
Smooth and Smoothest beside Instant and fails what the look-ahead made worse ("Smoothing compared with
Instant").

**Two replays have limits above their target:**
- `replay_queued_late_frames` at every smoothing (target 4%)
- `replay_false_retiming_featureless_cockpit` at Instant (target 1%)

The 64×32 thumbnails that the slot check judges by can't tell the lags apart there. Each limit sits just
above its worst measured run, so a regression still fails. The dust replay's worst frame is still
0.69–0.74° at Instant (about 16 px in a 1080p recording) and 0.38° with Smooth and Smoothest.

## How it works

1. **Fake PVR runtime** (`fake_pvr\`). A stand-in `libPVRClient64.dll` is copied next to a private copy
   of the mirror app (`out\<scenario>\simoptix_vr_mirror_e2e.exe`). Windows loads DLLs from the exe's
   folder first, so the real app uses the fake without any code changes. The fake mimics what was
   measured on a Pimax headset (a Dream Air) with LMU:
   - a 90 Hz display, with the predicted display slot two vsyncs ahead
   - LMU's left eye: 2508×2084, its field of view and its 2° eye cant, stored double-gamma-encoded like
     the real client mirror
   - the frame handoff: a legacy shared D3D texture plus a manual-reset event, with frames arriving
     about one frame after they start (±0.5 ms noise)
   - head poses from a motion spec, with constant-velocity prediction for future times
2. **Fake game.** Each frame is drawn with the head pose predicted for its display slot, taken when the
   slot is announced.
   - The runtime predicts each slot once, from that moment. The mirror app asks a fraction of a
     millisecond later and gets the same pose.
   - Instead of a scene, every pixel's colour encodes the world direction it shows
     (`0.5 + 0.5 * direction`), so the image looks like a shifting colour gradient.
   - The code is stored in 8-bit sRGB (about 0.7° of direction per step), with up to ±1 step of
     per-pixel noise (dither). Without the dither, the runner's 32×32 patch averages spanned only 3–4
     steps and were off by up to about 0.2° for some view directions.
3. **Capture like OBS.** The runner records the window with Windows Graphics Capture, the API behind
   OBS's Window Capture "Windows 10 (1903 and up)". It reads eleven 32×32 patches of the window:
   - the centre and a quarter width left and right (where the view points and how level it is)
   - the middle of each edge and each corner, starting a pixel inside the edge

   An edge or corner patch shows a black border when 3 or more pixels of its outermost line hold no
   direction code. The outermost line is the column or row next to the window's edge, or both at a
   corner. A border of about 1.6 px is caught at 320×180.
4. **Mirror app trace.** The mirror app writes a line per presented frame to the file named in
   `SIMOPTIX_VR_MIRROR_TRACE`. It holds times, the head pose it corrected with, the view it meant to
   show, the slot the frame arrived for, and the slot lead. The pipeline's observer writes it
   (`vr_mirror\core\pipeline\observer\README.md`).
   - The runner reads its columns by name. The pose the frame was warped from is `img` (`raw` in older
     builds). The runner skips the `#stats,` lines.
   - The mirror app draws once per game frame, so a line is a game frame. The runner matches each
     captured image to the frame it shows.
5. **The OpenXR layer's frames** (`fake_openxr\`, scenarios `openxr_*`). With `frame_source=openxr`, the
   mirror app reads the frames the way it reads the layer in the game. The runner starts
   `fake_openxr.exe` beside it. This fake game makes a game's OpenXR calls into the layer's own frame
   code (`vr_mirror\frame_sources\openxr\openxr_layer_frames.*` and `openxr_publisher.*`). It has a
   swapchain of three images. For each frame it acquires an image, draws into it, releases it and ends
   the frame. The layer picks the image each frame shows, and hands it over as in the game: the shared
   memory, the frame records, the three textures, the copy and its "done" marker, the clock reading.
   - It draws the left eye at 5084×4168 in LMU's field of view (52.6° out, 47.6° in, 44.6° up and down)
     with the shared image, encoded once as a game draws it. Both eyes are turned 1° out.
   - Its XrTime is 123.456789 s ahead of the performance counter, so the mirror app must use the clock
     reading.
   - Its timing is fake_pvr's, and `drop_rate`, `queue_lag` and `game_start_s` work the same. When it is
     behind, a frame still reports the slot it was drawn for.
   - With `render_thread=1`, one thread draws each frame and releases its image as soon as the frame
     begins. Another thread ends the frames, and the fake runtime holds each xrEndFrame until the
     frame's vsync. So the next frame's image is released while the frame before is still in
     xrEndFrame. Both threads use the one device context, protected for that as a game must protect it.
   - With the render thread, `stall_every_s` and `stall_ms` make a frame that much slower to draw that
     often. It ends late for the slot it was drawn for, and the frames due meanwhile never come. The
     next frame follows it at once.
   - Its log has fake_pvr's format. The runner matches its frames with the mirror app's by the slot
     they were drawn for (the trace's `drawn_slot`), because the mirror app sees them a frame or two
     after the game ended them.
6. **Replays of real sessions** (`vr_mirror\e2e\replay\<issue>.csv`). The fake plays back a real
   session taken from the OBS plugin's frame trace:
   - the head poses
   - the runtime's own pose predictions and display slots
   - every game frame at its real arrival time

   Each frame is drawn with the prediction for the slot the game really rendered it for. The slot was
   found by matching the recording to the trace frame by frame.
7. **Real images in replays** (`vr_mirror\e2e\replay\<issue>.thumbs.png`). The slot check looks only at
   64×32 thumbnails of each frame. Each replay brings the real ones, rebuilt from the session's
   recording with `vr_mirror\recording_analysis\export_thumbs.py`.
   - There is one tile per game frame, with the plugin's warp undone using the trace's poses and sampled
     like the mirror app does.
   - The fake draws that frame's tile as flat blocks exactly where the mirror app makes its thumbnail
     (the middle of the eye image). So the real slot check sees the real image.
   - The recording covers about 80% of that area. It shows the crop at zoom 1.3, not the bottom rows.
     The rest is a smooth extension of the covered colours.
   - Since the middle of the image shows thumbnails, replays are judged by poses, which are exact there,
     and not by reading the view on screen.

## What is checked

Bold means graded in at least one scenario.

| Group | Measure | Meaning |
|---|---|---|
| frames | mirror app frames/s, **game frames the mirror app missed** | The mirror app shows every game frame |
| | mirror app frames repeating one, skipping one | The share of the mirror app's frames that show the game frame before again, and that move past a game frame without showing it. Both are 0 except for the missed frames, because the mirror app draws once per game frame (info) |
| | arrival to present, its spread | How long after its arrival each game frame is shown (median), and the spread of that (1st to 99th percentile). With Smooth and Smoothest it is the output delay, which the OBS plugin's audio sync adds to the sound, so it should hold steady (info) |
| | captured frames/s, 60 fps OBS repeats | What window capture delivers (info) |
| smoothing | **smoothed view jitter**, **jitter ratio smoothed/head** | Jitter is what's left after fitting a smooth curve (local quadratic, ±50 ms). Deliberate turns count as zero, and tremor, vibration and shake (above about 5 Hz) count fully. There is an absolute limit where the head itself barely jitters |
| | **stalls mid-turn** | The view slows to a near stop, then catches up the same way. Slowing to turn around with the head doesn't count |
| | **recorded view jitter** | The view's jitter as a 60 fps recording shows it, from the mirror app's trace (exact, no colour decoding). At each tick it takes the newest frame presented. A frame held over because the game dropped one, or presented late, counts as it looks. So does the judder of showing 90 fps at 60, which is most of it while the head moves. It is averaged over six tick phases, because where the ticks fall is chance, and alone it moved one run with drops by 15% |
| | max lag behind head | How far the view trails during fast turns (info) |
| before the game started (`game_starts_late`) | **runtime calls/s while no game ran**, **first game frames missed** | The calls the mirror app made into the fake runtime from 2.5 s after switching the mirror on until the game's first frame, which is the load on Pimax's compositor while idle. And the game's first frames the mirror app didn't show |
| | **visible pose jumps**, largest jump | Graded in replays and `queued_cockpit`. The share of game frames whose pose error changed by more than 0.1° from the frame before (1.8 px of a 1080p recording at zoom 1.3), and the largest change. A steady error moves the whole image with the head and isn't seen. A change shows as the image jumping for a frame, which is what the shake study of real recordings counted (info) |
| poses (replays, `queued_cockpit`) | **frames from the wrong pose**, pose error (rms), worst frame's pose | For every game frame, the head pose the mirror app warped it from against the pose it was really rendered with. This is exact, because the fake predicts each slot once. It is measured in replays and in the synthetic scenarios whose game falls behind (`Scenario::posesExact`) |
| | **pose drift**, slow pose error | The pose error's mean over the run, in degrees. A drawn pose that corrects poses from the image (the cockpit correction) follows the image from frame to frame and the head poses in the long run, so its error averages out unless it drifts. Slow pose error is the largest 1 s mean of the pose error (info) |
| on screen (synthetic) | **view width**, off its zoom | The angle between the two patches a quarter of the window either side of the centre, which is the view's width (median), against the width the zoom asks for. The zoom shows 1/zoom of the image's half-angle on each side of the image's middle. The runner works that out from the image's edges (`runner\fake_log.h`), apart from the mirror app's own maths. Limit 0.2°, and the capture reads about 0.08° narrow |
| | image/pose mismatch | On screen against intended, where the view's middle looks at the middle of the image (rms, with a loose 1.5° limit against gross breakage). Each capture is paired with the closest of the last five frames' views. On Smoothest's smooth path this hides part of the image-pose error (0.06° against Instant's 0.55° with the same pose error). A capture is never paired with a frame twice, so held-over frames don't show here, and recorded view jitter covers them |
| | view jitter (on screen), jitter ratio view/head | What a viewer finally sees (info) |
| | **max horizon tilt**, **looking up or down** | The view's worst tilt, and its worst tilt on the frames that look more than 15° up or down. The screen looks along the head turned by the eye's cant and towards the image's middle, and such a camera tilts as the head pitches unless it is levelled itself. Graded in `head_still`, and while pitched in `black_borders` and `openxr_black_borders` (limit 0.5°) |
| | **frames with black borders** | An edge or corner of the window shows no game image. The count must be 0. It is not measured in replays: their real image covers nearly all of the window (u 0.2–0.8, v 0.25–0.85 of the eye, against about u 0.17–0.83, v 0.28–0.72 shown), and its dark pixels hold no direction code (the corners read 85–100% "black" on a good image) |
| | edge limit acted, zoom | How often the smoothing's last resort stopped the view at the edge of the shown image (graded in `black_borders_low_zoom`), and whether the zoom kept its setting (info) |
| | edge correction planned | The frames whose look-ahead path was pulled in from an edge coming in its window (info) |
| compared with Instant | **the measures above, Smooth and Smoothest beside Instant** | See "Smoothing compared with Instant" |

The look-ahead runs compare each frame's view with the head at the frame's own moment. The mirror app
presents the frame its look-ahead delay later (`vr_mirror\core\pipeline\4_smoothing\lookahead_smoothing.h`,
`LookaheadOutputDelayS`: 534 and 1034 ms from arrival). So the runner takes the head at the presented
time minus that delay (0 for Instant).

## Scenarios

Each scenario runs at all three smoothings with the same limits unless the table says otherwise.
Replays at Smooth and Smoothest also check that each held frame still gets the slot check's decision,
though the frame is shown up to a second after the slot check's short memory moved on.

| Name | What | Checks |
|---|---|---|
| `stabilization` | Racing glances (±25° sway, 35° glances) with a fast shake (23 and 31 Hz) | No stall. Shake at most 50% of the head's (Smooth, Smoothest: 10%) |
| `dropped_frames` | `stabilization`'s motion with the game missing 5% of its frames at random (`drop_rate=0.05`: one now and then, sometimes two in a row) | No stall. Shake at most 70% of the head's (Instant gets shakier with drops: 0.49–0.59, and Smooth and Smoothest: 10%). Mirror app at least 80 fps. Compared with Instant: recorded view jitter |
| `head_still` | Headset worn and head held still: pulse, breathing, head tremor (6–12 Hz) with a fine shake (13–19 Hz), slow sway, and a slow ±0.6° lean towards a shoulder. The jitter measure sees 0.012° of it, within the 0.007–0.017° of the real heads in the replays | Smoothed jitter at most 0.005°, horizon level (at most 0.2°), no stall |
| `black_borders` | One 12 s cycle of everything that pushes the view to an edge: 70° turns at about 315°/s, 35° flicks up and down, diagonal flicks, a 25° tilt, a fast shake, and a held turn with tilt | No black at any edge or corner, no stall (Smooth, Smoothest: jitter at most the head's) |
| `black_borders_low_zoom` | `black_borders`' motion at zoom 1.2, where the view meets the edge | No black, no stall. Smooth and Smoothest: the edge stop acts on at most 0.5% of frames (the path is pulled in ahead of time instead), and jitter at most 1.2 and 0.7 of the head's |
| `game_starts_late` | The mirror is on 5 s before the game's first frame (`game_start_s=5`). The texture and frame signal exist and the signal stays low, as on Pimax with no game | Runtime calls at most 50/s while no game runs (full rate is about 2,800/s), at most 1 of the game's first frames missed, no game frame missed after. Compared with Instant: its frame measures only |
| `queued_cockpit` | The game behind, as at 82 fps in a real race (`queue_lag=2`, `drop_rate=0.09`). It misses 9% of its slots and draws frames for up to two slots before the one they arrive for, and the lag changes at drops and catch-ups. The image is a detailed cockpit (`cockpit=1`). The head glances about as in racing without road vibration (`glances+head_tremor+heartbeat`). The cockpit correction is on (`drawn_pose=2`). Judged by poses | Visible pose jumps at most 2%, pose drift at most 0.5°, no stall, mirror app at least 75 fps |
| `queued_cockpit_slot_check` | The same with the slot check alone (the default), for comparison | Not graded |
| `replay_queued_late_frames` | The game falls behind and queues frames: a real race at Imola, into a dust cloud, with LMU two frames behind for a second | Frames from the wrong pose at most 12%, 7% and 10% (Instant, Smooth, Smoothest. Target 4%) |
| `replay_false_retiming_head_turns` | On-time frames re-timed while the head turns, misled by moving content: a real race at Monza, cornering at a steady 90 fps, every frame on time | Frames from the wrong pose at most 1% |
| `replay_false_retiming_short_hiccup` | A short timing hiccup opens the timing gate and on-time frames are re-timed: a real session at Monza, three arrivals about 7 ms late at 1:57, no queue | Frames from the wrong pose at most 1% |
| `replay_false_retiming_featureless_cockpit` | A featureless cockpit leaves the slot check only noise: a real race at Monza (2026-10-23, 18:31), a slow corner with one late arrival, every frame on time | Frames from the wrong pose at most 2.5% at Instant (target 1%), and 1% with Smooth and Smoothest |
| `replay_late_frame_same_slot` | One frame 7.4 ms late that the arrival timing rounded onto the next frame's slot. Two frames shared a slot and the first was lined up from the next slot's head pose, a one-frame jolt of 3.5 px. A real race at Le Mans (2026-11-04, 7:39 of 19-35-51_s2) | No frame from the wrong pose |
| `openxr_stabilization` | `stabilization`'s motion from the OpenXR layer's frames | As `stabilization`, and no frame from the wrong pose |
| `openxr_dropped_frames` | `dropped_frames`' game, from the layer | As `dropped_frames`, and no frame from the wrong pose |
| `openxr_game_behind` | `queued_cockpit`'s game (`queue_lag=2`, `drop_rate=0.09`) with the direction codes, from the layer. Frames come up to two slots late for the slot they were drawn for, and they report it | No frame from the wrong pose, no visible pose jump, no stall, mirror app at least 75 fps |
| `openxr_black_borders` | `black_borders`' motion in the layer's field of view | No black at any edge or corner, no stall |
| `openxr_game_starts_late` | The layer's memory appears 5 s after the mirror app starts (`game_start_s=5`) | At most 1 of the game's first frames missed (the first, because the layer learns the image's size from it) |
| `openxr_stall` | `stabilization`'s motion from a game with a render thread (`render_thread=1`), and a 64 ms stall every 3 s. Each frame's image is released before the frame it follows has left xrEndFrame | Each frame shows its own image: image/pose mismatch at most 0.2° rms and 0.5° in the worst frame, mirror app at least 80 fps |

## Smoothing compared with Instant

After the runs, each scenario's Smooth and Smoothest results are set beside its Instant result. A
measure the look-ahead made worse fails, beyond the spread of repeated runs of one build:

| Measure | May exceed Instant's by |
|---|---|
| game frames the mirror app missed | 0.5 points |
| first game frames missed, stalls mid-turn, frames with black borders | Nothing |
| smoothed view jitter | 0.005° (`head_still`'s limit, a tenth of a pixel) |
| jitter ratio smoothed/head | 0.005, and never while the smoothed jitter is at most 0.005° |
| recorded view jitter | 10% + 0.005° (repeated runs: within 0.003°) |
| frames from the wrong pose | 40% + 0.3 points (Instant alone: 6.9–9.5% in `queued_late_frames`) |
| max horizon tilt | 0.05° |

The rest is shown beside them: frame rates, delay and its spread, frame gaps, OBS repeats, lag,
mismatch, on-screen jitter, pose error and edge limit. These differ by design (the delay, the lag) or
are informational.

- The mirror app's frame rate follows the game's, which varies between runs. A fake game that stalled
  for 144 ms took one run to 87.2 fps. The frames the mirror app missed say whether it kept up.
- `game_starts_late` compares only its frame measures, because its view is a still head over the few
  seconds after a start.
- Instant's dead zone holds a slow view completely still. So its smoothed jitter in replays reads 0.000°
  where the look-ahead leaves 0.001–0.002° (0.02 px). Its recorded judder is lower where the head moves
  slowly (`short_hiccup`: 0.016° against 0.021°). The allowances above cover that and nothing visible.

## Stage replay

`stage_replay` runs core's frame pipeline offline, without the mirror app, the fake runtime or the GPU.
It runs on the replays, with each frame drawn with its true slot's pose. It also runs on synthetic
motion (`edge_test+heartbeat`, `racing+heartbeat`, with exact pose predictions, about 3% dropped frames
and a few unsynced ones). It feeds poses and predictions to core's pose history in time order, up to the
moment a host draws, as the mirror app does live. It is deterministic and takes seconds. It can do
three things:

- **Replay one stage alone on real inputs.** It replays the drawn pose on a replay's real
  thumbnails, slots and pose predictions, and scores it against the poses the frames were really drawn
  with, for each way a host asks for them. It also replays the arrival timing and the frame picker over
  the OBS plugin's frame traces.
- **Run the whole pipeline with any mix of stages.** It draws as the mirror app's window or as the OBS
  plugin's ticks, and scores from the pipeline's numbers alone (`scores.*`): black corners, lag, jitter
  while still and in small moves, edge stops and planning, and frames from the wrong pose.
- **Write every output of the pipeline** as a golden file, each float exactly, over a fixed matrix of
  inputs and settings. A change that must not change behaviour can be checked byte for byte against
  the build before it.

## The local files test

`bin\local_files_e2e.exe` checks the frame traces, crash dumps and OpenXR layer logs that SimOptix
keeps in `%LOCALAPPDATA%\simoptix`. It runs the plugins and the layer as built, with
`%LOCALAPPDATA%` pointed at a test folder that already holds older files of each kind.
- **The plugins** run in OBS 32.2.2, in a copy of the installed OBS in `out\obs_portable\`, made on the
  first run. The copy runs in OBS's portable mode, with settings of its own made fresh for each run.
  It loads the plugins from this build and no installed plugin, so the user's own OBS stays as it was.
  The Pimax source reads the fake runtime, which sits beside the copy's `obs64.exe`.
- **The layer** runs in `bin\fake_layer_game\Le Mans Ultimate.exe`. The fake game loads the layer's DLL
  and makes the OpenXR loader's first call to it, without a runtime or a headset.

| Case | What runs | Checks |
|---|---|---|
| `no_game` | OBS with both sources and Debug on. The fake runtime runs with no game | No frame trace. Of 12 older crash dumps, the newest 8 are left once the plugins have loaded |
| `pimax_game` | The Pimax source with the fake runtime's game | One new trace, both its files with rows. Of it and 12 older traces, the newest 10 are left, each older one gone with all its files |
| `openxr_game` | The OpenXR source with `fake_openxr.exe` | One new trace, both its files with rows. Of it and 10 older traces, the newest 10 are left |
| `layer` | The fake game three times: with the first OpenXR call, without it, and with it again | Each start has its own log, which starts with the load line. Of 12 older logs and the new ones, the newest 10 are left after each start with the call. The layer's logs of older names stay |

Each case also checks that files of other names stay. OBS and LMU must be closed. A run takes about a
minute, and the first run takes longer while it copies OBS. Each case's files, OBS's log among them, are
in `out\local_files\<case>\`.

## Files

| Folder or file | What |
|---|---|
| `build.bat` | Builds the fake runtime, the fake OpenXR game, the runner, the local files test with its fake game, and `stage_replay` into `bin\` |
| `fake_game\` | What both fakes share: the head motion (`fake_game_motion.h`) and the game's image (`fake_game_picture.*`: the direction codes or the cockpit, stored as the faked source stores it) |
| `fake_layer_game\` | `Le Mans Ultimate.exe`, for the local files test: the fake game that loads the real OpenXR layer's DLL |
| `fake_openxr\` | `fake_openxr.exe`: the fake game with the OpenXR layer's frames, through the layer's own frame code (`fake_openxr_layer.*` makes the game's calls into it) |
| `local_files\` | `local_files_e2e.exe`: the local files test. Its cases (`local_files_e2e.cpp`), its copy of OBS (`local_files_obs.*`), and the older files it makes and the files it reads (`local_files_folder.*`) |
| `fake_pvr\` | The fake PVR runtime and game (`libPVRClient64.dll`): replays of real sessions (`fake_pvr_replay.h`) and their real thumbnails (`fake_pvr_thumbs.h`) |
| `runner\` | `e2e_runner.exe`: the scenarios (`scenarios.*`), running the mirror app (`tool_process.*`), capturing its window (`capture.h`, `patches.h`), the measures (`analysis.*`), and the comparison with Instant (`compare.*`) |
| `stage_replay\` | `stage_replay.exe`: one run per stage (`runs_*.cpp`), the whole pipeline (`runs_pipeline.cpp`), its scores (`scores.*`), and the golden file (`golden.*`) |
| `replay\` | The replays of real sessions (`<name>.csv`, each a scenario `replay_<name>`) and their real thumbnails (`<name>.thumbs.png`) |
