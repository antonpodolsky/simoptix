# Recording analysis

These scripts check a recording frame by frame. They measure how much the cockpit shakes, when the
game's frames came late or repeated, and, with the OBS plugin's frame trace, what the plugin did on
each frame and whether the video agrees. They find the moments behind the e2e replay tests and export
them. One command (`analyse_recording.py`) analyses a whole recording, and the other scripts look
closer at one moment. They need the trace that the Pimax source's **Debug** setting writes
(`vr_mirror\obs_plugin\README.md`, "Debug: stamp and frame trace").

## Terms

| Term | Meaning |
|---|---|
| stamp | The frame number that VR Mirror draws as 15 small squares in the top-left corner of the view, when its Debug setting is on |
| lag | How many display slots earlier than its arrival slot the game drew a frame. 0 is on time. A lag appears while the game is behind |
| slot check | A way to find the drawn pose: it judges from thumbnails which display slot a frame was really drawn for |
| replay | A real session played back through the fake runtime: its head poses, the runtime's predictions, and every game frame at its real arrival time and with the slot it was really drawn for |
| thumbnail | A 64×32 colour image of the middle of an eye image. The slot check compares them |
| mirror app | `simoptix_vr_mirror.exe`, VR Mirror in a desktop window (`vr_mirror\mirror_app\`). The e2e tests run it |
| frame trace | The CSV file that the Debug setting (or the mirror app's `SIMOPTIX_VR_MIRROR_TRACE`) writes, with one row per drawn frame: its poses and timing. With the Pimax source, Debug also writes a second file of head poses, predictions and arrivals. With the OpenXR source, the second file holds the frames the layer published and how the game handed their images over |
| edge stop | The smoothing's last resort. It holds the view at 95% of its image room when the smoothing's own planning wasn't enough |
| display slot | One of the regular moments at which the headset shows a new image, one per refresh (90 per second at 90 Hz). The game draws each frame for one slot, with the head pose the runtime predicted for that slot |
| tick | One time OBS draws a frame of its output |

## Files

| File | What |
|---|---|
| `trace_common.py` | Reading the trace and the OBS log, quaternion maths, the frame stamp, and ffmpeg. It also finds the slot each frame was drawn for (`FrameLag`), from the trace's own column in traces with 18 columns and by comparing the image pose with the runtime's predictions after |
| `episodes.py` | The plugin's re-timed stretches in a recording, from the trace |
| `window.py` | A stretch of video against the trace: the true lag per frame, from the cockpit's motion |
| `replica.py` | The slot check replayed offline on a recording's real images |
| `export_replay.py` | A stretch of the trace as an e2e replay file |
| `export_thumbs.py` | A replay's real thumbnails, rebuilt from the recording (`<replay>.thumbs.png`) |
| `thumbs.py` | Rebuilding the mirror app's thumbnails from a recording (used by `replica.py` and `export_thumbs.py`) |
| `e2e_frames.py` | The frames of an e2e replay run that came out from the wrong pose |
| `patch_shift.cpp` | The frame-to-frame shift of image patches in a raw greyscale clip (`build.bat`) |
| `analyse_recording.py` | A whole recording with one command: finds its log and trace, runs the steps below, and writes `summary.txt` |
| `sources.py` | Which OBS log recorded a recording, and which frame trace was open then |
| `grid_shift.cpp` | The frame-to-frame shift of a grid of 88 patches over a whole recording, read from ffmpeg (`build.bat`) |
| `shake.py` | The cockpit's rotation per frame from those shifts, its path and its jumps |
| `shake_causes.py` | The jumps by kind of frame and against the log's 10 s stats (no trace needed) |
| `trace_ticks.py` | A whole trace read into one layout, whatever its format (cached) |
| `lineup.py` | Which trace row each video frame shows. It chooses the stamp's occurrence by the repeated frames |
| `shake_traced.py` | Each jump's cause from the trace: the view's own motion, an edge stop, or an image lined up from the wrong pose |
| `cadence.py` | Ticks, game frames and display slots step by step, to find where the scenery's steps go uneven |
| `events.py` | Repeated game frames, missed slots, two frames on one slot, and frames never shown, with the cockpit's jump at each |
| `moment.py` | One moment frame by frame: the trace's rows and the runtime's arrival rows |
| `aligned_diff.py` | The road's change between frames with the cockpit lined up (frozen or skipped game frames) |
| `frozen_check.py` | The video-only repeat detector's finds checked against the trace |

## How the whole-recording analysis measures

**Cockpit jumps.** The cockpit is fixed in head-tracking space. A sim rig has nothing that moves the
head but the driver. So on screen the cockpit moves only with the view, plus any error in the head pose
that a game frame was lined up from.
- `shake.py` fits one rotation per frame to the patches that agree (the cockpit).
- A jump is the cockpit's step away from (or back to) a smooth curve through the 7 frames either side.
- Units are pixels of the 1920x1080 image: 17.9 px per degree at the centre, with roll counted at 540
  px from the centre.
- A step over **2 px** counts as a visible jump. The measuring noise is far below that (median step
  about 0.25 px in a clean race).

**Lining up with the trace.** Each frame carries its tick's number stamp (top-left). The stamp wraps
every 32768 ticks (6 minutes), so a recording fits every 6 minutes of a long trace equally well.
- The OBS log's clock gives a first guess, but it can be minutes off. For the recording on Instant
  in the table below it is 481 s late (more than one wrap), because the trace's first head pose was written long after
  the log says the trace opened.
- `lineup.py` therefore tries every place where the stamps fit and checks the images. On a repeated
  game frame, everything in the image moves with the cockpit, so the patches' spread around the
  cockpit's motion dips below that of the frames either side.
- At the right place, most of the trace's single repeated frames fall on such a dip. At a wrong place,
  only the few that chance puts there do (any frame dips with a chance of about 4%).
- It picks the place where that many dips would be least likely by chance. A higher "unlikely" in
  `lineup.txt` is better.

| Recording | Single repeats on a dip | Chance at the chosen place | At the next best place |
|---|---|---|---|
| A session on Instant, 217 repeats | 174 of 174 | 1 in 10^251 | 1 in 10^6.0 |
| A clean race on Look-ahead, 18 repeats | 6 of 8 | 1 in 10^7.4 | 1 in 10^1.3 |

When the trace has too few single repeats to tell, the script takes the log's guess, and `lineup.txt`
then says **NOT SURE**. `lineup.txt` also checks the result: shifted by a frame or two, the repeats
should miss.

**Trace formats.** The scripts read all of these:
- 18 columns, the oldest: no zoom, edge counters or view time, so `shake_traced.py` is skipped
- 26 columns
- 31 columns, with `edge_repeat_clamps`, for a short time
- 30 columns, the newest, with `pose_notes` last (`picture_notes` in older ones, and `drawn_pose`
  `picture_pose`)

## Known limits

- **The video-only repeat detector (`shake_causes.py`) raises false alarms in a clean race.** It calls
  a frame repeated when 90% of the patches follow the cockpit within 0.6 px.
  - Real repeats read 0.12 to 0.31 px, but new game frames under a slow view can read 0.48 to 0.60.
  - In the clean race on Look-ahead (above) it flagged 381 frames (128 of them during the race), and the trace
    confirms 10 of them. The trace has 18 repeats in all.
  - So its count is not a count of freezes. The trace's serials are (`events.py`, and the "repeated
    game frames" line of `summary.txt`). The summary says how many of the detector's finds the trace
    confirms.
- `followed_serial` in OBS traces is the OBS tick's own id (2^62 plus the tick count) wherever the view
  follows the tick's moment rather than a game frame. That is every tick in both recordings above.
  `shake_traced.py` counts no image as shown for another frame there. The earlier scripts read every
  such tick as one (88114 of 88130 in the clean race).
- The trace doesn't hold which slot the game really drew a frame for. `shake_traced.py` infers it from
  the cockpit's motion where the head moved enough to tell.
- Shake rows are labelled with video times (k + 1) / 90 s, as in the earlier investigations. So a
  moment's time can be one frame (11 ms) off the player's. The stamp decides.
