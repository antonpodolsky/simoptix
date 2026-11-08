# SimOptix: VR Mirror, the mirror app

`simoptix_vr_mirror.exe` shows the stabilized headset view in a desktop window. It runs the same core
(`vr_mirror\core\`) and frame sources (`vr_mirror\frame_sources\`) as the OBS plugin
(`vr_mirror\obs_plugin\`), which draws the view inside OBS and is the way to record. The mirror app is
the program that the end-to-end tests run (`vr_mirror\e2e\`). The tests start it any number of times
against a fake PVR runtime or a fake game with the OpenXR layer's frames, and capture its window the way
OBS captures a window.

## Terms

| Term | Meaning |
|---|---|
| game eye | The eye image a frame source delivers. The Pimax source's is a half-size copy of the left eye (2508×2084 in LMU at Render Quality 1.0). The OpenXR source's is the left eye at full size (5084×4168) |
| client mirror | A feature of the Pimax runtime. It gives other programs a half-size copy of the left eye image the game draws, and a signal for each new frame |
| display slot | One of the regular moments, 90 per second, at which the headset shows a new image. The game draws each frame for one slot, with the head pose the runtime predicted for that slot |
| game behind | The game's GPU has fallen behind and queues up to two frames. Each arrives one or two display slots after the slot it was drawn for |
| slot check | A way to find the drawn pose: it judges from thumbnails which display slot a frame was really drawn for |
| cockpit correction | A refinement of the drawn pose. It measures how the fixed cockpit moved between two frames and corrects the pose by what the head poses don't explain |
| frame trace | The CSV file that the Debug setting (or the mirror app's `SIMOPTIX_VR_MIRROR_TRACE`) writes, with one row per drawn frame: its poses and timing. With the Pimax source, Debug also writes a second file of head poses, predictions and arrivals |
| fake runtime | The stand-in for Pimax's client library (`vr_mirror\e2e\fake_pvr\`) that the e2e tests use in place of a headset and a game |
| frame source | The code that delivers game frames, head poses and timing to VR Mirror: the Pimax source (`vr_mirror\frame_sources\pimax\`) or the OpenXR source (`vr_mirror\frame_sources\openxr\`) |
| PVR | Pimax's own name for its client interface: the client library `libPVRClient64.dll` that Pimax Play installs, and the table of functions it hands out. The project's code that wraps it is named after it |

## How it works

- **Frames:** the Pimax game eye from the client mirror, or the OpenXR layer's full-size eye image,
  kept as large as the window shows it (`frame_source`, below, and `vr_mirror\frame_sources\README.md`).
- **Drawing:** once per game frame.
  - With Instant (smoothing 1), each frame is copied and drawn as soon as it arrives.
  - With Smooth and Smoothest (2 and 3), each frame is copied as it arrives, held, and drawn half a
    second or a second later, when the head's movement after it is known (`frame_hold.*`). The hold
    keeps a copy of every frame in that time: about 1.1 GB of video memory for half a second at Render
    Quality 1.0, and 2 GB for a second.
- **Paced presents:** each frame is presented 4 ms after its de-jittered arrival time, so presents are
  evenly spaced. In the e2e tests the spacing is about 0.13 ms rms, against about 1 ms when presenting
  right away. A window capture grabs the newest present on its own clock. Presents that wobble
  around its grab time make it repeat and skip frames for minutes at a time. If the console reports
  frames drawn after their present time, the machine was too busy.
- **Late game frames:** frames the game rendered for an earlier display slot (when it has frames
  queued) are found by their images and warped from that slot's pose, as in the OBS plugin. The console
  says every 10 s how many frames were judged and how many were late ("game behind: ...").
- **No game running:** after 2 s without a game frame, the app idles. It stops asking Pimax for poses
  and timing (about 2,500 calls a second otherwise), stops redrawing, and only waits for the game's
  next frame, which it catches. The title says "no game frames (idle)".
- **The title** shows the smoothing, the zoom, the game's frame rate and the state.

On exit the app switches the mirror off. If it crashes, its crash handler does.

## Settings

The app reads `simoptix_vr_mirror.ini` next to the exe, section `[mirror]`, at start. To change a setting, edit the
file.

| Key | What |
|---|---|
| `frame_source` | Where the frames come from: `pimax` (the default, Pimax's client mirror) or `openxr` (the OpenXR layer in the game, `vr_mirror\frame_sources\openxr\`) |
| `smoothing` | 0 Off, 1 Instant, 2 Smooth, 3 Smoothest (the default), as in the OBS plugin |
| `zoom` | The crop factor (default 1.5). The spare image around the view is how far the view may trail the head |
| `drawn_pose` | How the head pose a frame was drawn with is found: 0 off, 1 the slot check (the default), 2 the slot check plus the cockpit correction (the OBS plugin's choice) |
| `output_width`, `output_height` | The window's client size at start (default 2560×1440) |

Two environment variables:
- `SIMOPTIX_VR_MIRROR_TRACE=<file>` writes the frame trace: one row per presented frame, in the columns
  of `vr_mirror\core\pipeline\observer\README.md`.
- `SIMOPTIX_VR_MIRROR_SERVER_EXE=<exe>` names the process that owns the frame signal (default
  `pi_server.exe`). The e2e tests' fake runtime uses it.

## Files

| File | What |
|---|---|
| `main.cpp` | The loop: the Pimax source, the pipeline, when to draw and present |
| `window_output.*` | The device, the swap chain and the warp shader |
| `mirror_app_settings.*` | `simoptix_vr_mirror.ini` |
| `frame_hold.*` | The held frames for Smooth and Smoothest |
| `precise_wait.*` | The timer waits |
| `build.bat` | Builds `simoptix_vr_mirror.exe` with the frame source, the core and `shared\` |

## Known limits

- The app stabilizes rotation only. Head translation (leaning) is not stabilized.
- The game eye is always the left eye. The view is centred on that eye's axis, which is turned about 2°
  outward, so the wheel appears a few degrees right of centre.
- The game eye is a half-size copy of what the game renders. It grows with Pimax Play's Render
  Quality, at the game's GPU cost (`vr_mirror\obs_plugin\README.md`).
