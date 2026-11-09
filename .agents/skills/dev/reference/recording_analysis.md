# Analysing a recording frame by frame

Terms: `reference\terms.md`. What the scripts are and how they measure:
`vr_mirror\recording_analysis\README.md`. What was found with them: `findings\recording_analysis.md`.

Paths below are relative to `vr_mirror\recording_analysis\`. Run the scripts with `python -I`.

## Before recording

- In the Pimax source's properties, tick **Debug**. It stamps frame numbers and writes a frame trace
  (`vr_mirror\obs_plugin\README.md`, "Debug: stamp and frame trace").
- The trace goes to `%LOCALAPPDATA%\simoptix\traces\trace_<time>_*.csv`, about 300 MB per hour with
  the Pimax source. With the OpenXR source it is about 100 MB per hour, and its own file adds about
  30 MB (estimated from its rows' size, not measured). It starts at the game's first frame. Only the
  newest 10 traces are kept, so copy the trace of a recording to analyse into the data folder soon after
  recording.
- The OBS log (`%APPDATA%\obs-studio\logs`) says which trace belongs to which session.

## Setup

- Python with numpy.
- ffmpeg, with `ffprobe.exe` beside it. The scripts take the one `FFMPEG` names (the full path of
  `ffmpeg.exe`), else the one on PATH.
- `build.bat` builds `patch_shift.exe` and `grid_shift.exe` (Visual Studio's C++ tools).
  `analyse_recording.py` runs it by itself the first time.
- Extracted frames and trace parts are cached in `%TEMP%\simoptix_recording_analysis` (`TRACE_CACHE`
  moves it). Delete it when done: a few seconds of video are hundreds of MB, and a whole trace's parsed
  copy is about as big as the trace.

## A whole recording

```
python -I analyse_recording.py "<recording>.mp4" [--out <folder>]
```

**What it finds.** The OBS log that recorded the video: the one with a `==== Recording Start` within
5 s of the time in the file name (`_s1`, `_s2` at the end are fine). And the frame trace that was open
then: the plugin's last `frame trace:` line before that start.

**What it does:**
1. It decodes the video once, on the GPU, for two things: how 88 patches of the image moved from frame
   to frame (`grid_shift.exe`, on a 960x540 grey copy), and each frame's number stamp.
2. It fits the cockpit's motion and its jumps (`shake.py`), and sets them against the plugin's 10 s
   stats in the log (`shake_causes.py`).
3. With a trace:
   - it lines the video up with the trace (`lineup.py`, the README's "Lining up with the trace")
   - it says which cause each jump had (`shake_traced.py`)
   - it shows how the scenery's steps went (`cadence.py`)
   - it lists the timing events: repeated game frames, missed slots, two frames on one slot
     (`events.py`)

**What it writes.** Everything goes into the output folder. By default that is a new folder
`<recording> analysis` in the data folder (`SIMOPTIX_DATA`, `SKILL.md`, "5. Verify"), else beside the
recording.
- `summary.txt` has the headline numbers.
- There is one report per step (`shake.txt`, `lineup.txt`, `traced.txt`, `cadence.txt`,
  `events.txt`), with their data.
- `run.json` says which log and trace were used.

Run it again and the steps already done are skipped. `--redo` runs them all again. The first decoding
takes about 4 minutes per 16 minutes of video, and the first read of a trace a few minutes. `grid.f32`
(125 MB per 16 minutes) can go when the analysis is done.

**Repeated game frames:** when there is a trace, count them from the trace's serials (`events.py`,
`summary.txt`'s "repeated game frames"). Don't count them with the video-only repeat detector
(`shake_causes.py`), which raises false alarms in a clean race (`vr_mirror\recording_analysis\README.md`,
"Known limits").

## One moment

```
python -I moment.py "<output folder>" 7:39.57 [frames either side]
```

```
python -I aligned_diff.py "<recording>.mp4" 7:39.50 0.2 [--cockpit ... --road ...]
```

```
python -I frozen_check.py "<output folder>"
```

- `moment.py` prints the trace's rows and the runtime's arrival rows, frame by frame.
- `aligned_diff.py` says how much the road changed between frames once the cockpit is lined up: about
  1 on a frozen game frame, about 6 on a normal one, and double on a skipped one. Its default regions
  fit the car of the recordings analysed so far, at 2560x1440. Pick new ones for another car.
- `frozen_check.py` says which of the video's "frozen" frames the trace confirms.

## What the plugin re-timed in a recording

```
python -I episodes.py "<OBS log>" "<recording>.mp4" [--context 6]
```

It lists the log's "game behind" lines, and every stretch of frames the plugin drew from an earlier
slot's pose, with video times. `--context` adds the frame timing around each: the arrival gap, the slot
step and the slot lead.

## Which lag the video supports

```
python -I window.py "<OBS log>" "<recording>.mp4" 18:30 4 --patches "560,80,300,50;380,70,150,40;30,380,120,130"
```

- It lines the stretch up with the trace by the frame stamp.
- It measures how the cockpit patches moved, in a 960x540 frame. Pick patches on fixed parts of the
  car, not the wheel, screens, mirrors, windscreen or OBS overlays, 20 px from the edges. Pick new ones
  for each car, from a frame of the recording.
- It compares that with the motion each candidate lag predicts. The lag with the lowest step error is
  what the game really did.
- It also prints the exact video time offset for `episodes.py --offset`.

## Why the slot check decided what it did, on the real images

```
python -I replica.py "<OBS log>" "<recording>.mp4" 18:30 4 --gate --show 1.6-2.8 [--maps SERIAL]
```

It rebuilds the plugin's thumbnails from the recording, and runs the slot check's logic on them. That
logic is a Python copy of the slot check's helpers,
`vr_mirror\core\pipeline\2_drawn_pose\slot_check_drawn_pose_lag.cpp`: keep the two in step. Core's
own code runs on a replay's exported thumbnails with `vr_mirror\e2e\bin\stage_replay.exe --stage
drawn_pose`.

| Option | What it does |
|---|---|
| `--show <from>-<to>` | Prints the evidence per frame |
| `--maps <serial>` | Saves images of one frame's thumbnails and per-texel errors |
| `--truth` | Scores against known lags: the forward decisions, and on a second line the hindsight ones that held frames get with Look-ahead |
| `--clip-of "<recording>.mp4" --clip-start mm:ss` | Runs on a clip in place of the recording. The start is then a time in the clip |
| `--thumbs ..\e2e\replay\<issue>.csv` | Runs on that replay's exported thumbnails, which the e2e fake shows, instead of rebuilding them |
| `--fill` | Also counts the texels the recording didn't cover, as the mirror app in e2e does |

The penalty and detail options try other settings. This is how a change to the slot check is checked
on real images, which the e2e fake can't always imitate.

**Not an exact copy of core** (2026-10-25). It averages and compares thumbnails as displayed, while core
does both in linear light. Even in linear light it re-times far more frames than the mirror app does in
e2e on the same thumbnails (`findings\e2e.md`, "Real thumbnails in replays").

## A replay test from the moment

1. Export the stretch of the trace as a replay:
   ```
   python -I export_replay.py "<OBS log>" "<recording>.mp4" 18:25 12 ..\e2e\replay\<issue>.csv --about "..." [--lags "..."]
   ```
2. Export its real thumbnails, which the e2e fake shows so that the mirror app's slot check sees the
   real images:
   ```
   python -I export_thumbs.py "<OBS log>" "<recording>.mp4" ..\e2e\replay\<issue>.csv
   ```
   Add `--clip-of "<recording>.mp4" --clip-start mm:ss` when only a clip of the recording is left.
3. After an e2e run, `e2e_frames.py vr_mirror\e2e\out\replay_<issue>` lists which frames came out from
   the wrong pose, and the lag the mirror app used.

`reference\e2e.md`, "Adding a replay of a real session", has the rest of the steps and the file format.

## Older traces

The columns are in `vr_mirror\core\pipeline\observer\README.md`, and the formats the scripts read in
`vr_mirror\recording_analysis\README.md`. What changed, for reading older traces:

- **OBS traces before 2026-10-31** had the slot the frame was drawn for (`display_slot`) and the
  cockpit correction (`corr_*`), instead of `img`'s finding. `trace_common.py` (`FrameLag`) reads the
  drawn slot from that column there, and from comparing `img` with the predictions after.
- **`edge_clamps` until 1 Nov 2026** counted the clamp after the smoothing, against the followed image
  only. On 1 Nov 2026 a column `edge_repeat_clamps` stood before `edge_shift`, counting the clip against
  the shown image apart.
- **Until 2026-11-08** the columns `drawn_pose` and `pose_notes` were named `picture_pose` and
  `picture_notes`, at the same places. The scripts read the columns by place, so both read the same.
- **The mirror app's trace** had its own columns until 2026-10-31 (`reference\mirror_app.md`). The e2e
  runner reads the pose the image was warped from as `img`, else as `raw` in older builds.
