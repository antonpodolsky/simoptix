# Findings: VR Mirror's OBS plugin

Terms: `reference\terms.md`. What the plugin does and its settings: `vr_mirror\obs_plugin\README.md`.
This file holds what was measured on the plugin, and what was tried.

## Connecting and disconnecting

- Connecting to Pimax takes about 0.4 s, and disconnecting up to about 0.3 s.
- Both ran on OBS's graphics thread at first, and froze the whole output meanwhile: 30–50 lost or
  repeated frames per scene switch, estimated from the code. They run on a thread of their own since.

## Smoothing and zoom

- **Smoothest dropped, Smooth renamed Look-ahead (#22).** Smoothest ran a second behind the
  game, and the OpenXR source keeps a copy of every frame in that time. At the layer's full
  5084x4168 that is about 97 images and 8.2 GB of video memory, too much beside the game. Under the
  4 GB cap each image shrinks to about 3580 px wide, so a 3840x2160 canvas gets 74% of native at zoom
  1.25. Look-ahead, half a second, keeps about 52 images up to about 4840 px wide, native for 4K up to
  zoom 1.26. It gives up a little calm: its view trails the head by 0.95° at most 99% of the time,
  against Smoothest's 0.75° (`findings\mirror_app.md`, "Core's pipeline on the same poses"). Its
  outputs are unchanged, `stage_replay --golden` byte for byte against the build before for every
  Instant and Look-ahead run. Saved scenes with Smoothing 3 open on Look-ahead.
- **Smoothest against Instant**, on 4.3 h of real head poses (`findings\mirror_app.md`, "Findings:
  other smoothing methods"):
  - Smoothest's view trails the head by 0.75° at most 99% of the time. Instant's: 4.6°.
  - What tremor and tracking noise leave with Smoothest is under a tenth of a pixel.
  - Not checked in a recording.
- **Edges planned for with the look-ahead**, on e2e's fastest turns (315°/s) at zoom 1.2, at
  Smoothest (`findings\e2e.md`, "Edges planned for with the look-ahead"):
  - after: no frame stopped at the edge, and the view's shake 0.48 of the head's
  - before: 1.6% of frames stopped, and the shake 1.06 of the head's
- **The lowest zoom**, on 4.3 h of real head motion (`findings\mirror_app.md`, "Core's pipeline on the
  same poses"):
  - zoom 1.29: the edge is never reached
  - zoom 1.2: the edge is touched on about one frame in a thousand
  - zoom 1.1: the view gets shakier than with Instant

  Hence the advice in the README not to go below about 1.2 with Smooth or Smoothest.
- **The edges in the smoothing** (`findings\e2e.md`, "The edges in the smoothing"). The
  source draws on OBS's clock, so each tick shows a game image drawn for a slightly later moment, or
  the previous one again. Since this date the smoothing keeps the view inside that image and plans
  ahead for it. Before, a hard clip after the smoothing cut the view. With the mirror app drawing like
  OBS at zoom 1.2:

  | | Before | After |
  |---|---|---|
  | Frames stopped at the edge, Smooth and Smoothest | 12–13% | 0% |
  | The view's shake, as a share of the head's | 1.5–1.6 | 0.51–0.60 |

  Instant still stops on about a quarter of those frames.

## Audio sync

- **The shift, checked in OBS** (no game, so the delay stayed at its warm-up value):
  - with the source shown: LMU's sound −120 → +950 ms, Mic/Aux 0 → +1070 ms
  - with the source hidden: back to −120 and 0 ms
- **LMU's sound trails the image** by about 120 ms, so it needs about −120 ms of its own Sync Offset
  (on its Application Audio Capture).
  - Measured on LMU's menu clicks: 113–136 ms between the menu changing on screen (in one
    frame) and the click being heard.
  - The plugin shows each game frame 5 ms after it arrives. So the delay is in the game's sound or its
    capture, not in the image.
  - Not checked: drift over a long session.

## Race auto-record

- **It follows the view's delay since November 2026.** Before, the recording started when the game
  reached the start lights, so with Smoothest it began with about 1 s of formation lap.
- **The first 0.1–0.35 s of the lights are missing.** Auto-record checks four times a second, and OBS
  takes about 0.09 s to start recording (measured from the log).
- **Checked in OBS with `lmu_fake --race`:**

  | Build | Start after the lights | Stop after the car stood still |
  |---|---|---|
  | The switch still in the G-meter source | 0.24 s | 5.1 s |
  | The overlays on the telemetry model | 0.1 s | 5.0 s |
  | The switch in the Pimax source, no headset | 0.24 s | 5.25 s |
  | The same, with a 0.5 s delay written in for the headset's | 0.53 s | 5.54 s |

  With the first of these builds, a practice session with the same phases recorded nothing, as it
  should. The delay
  test is in `findings\telemetry.md`.
- **Not checked:**
  - that LMU's countdown phase begins exactly when "hold speed" shows
  - whether LMU keeps sending updates while the game is paused. If it doesn't, a pause of more than
    35 s stops the recording.

## Recording at 90 fps

- At OBS's 90 fps every game frame is used exactly once, so motion is even.
- At 60 fps each recorded frame advances the game by one or two frames in turn. The plugin keeps the
  camera (the head) smooth, but the car's own motion (cornering, speed) steps unevenly. Measured: a
  back-and-forth of about 2 px at 1080p while driving.
- Converting a 90 fps recording to 60 afterwards with motion interpolation (optical flow) avoids it.

## The game eye's size

- **The Pimax runtime has no size, crop or field-of-view option for its mirrors.** Its config keys for
  the client, screen and undistorted mirrors are only on/off, the texture handles and the sync handles
  (key names read from `pi_server.exe`). The field-of-view keys (`fov_level`,
  `fov_custom_*_percent`) change the headset's field of view, which is what the game draws, not the
  mirror.
- **A smaller headset field of view**, to win back the GPU that a higher Render Quality costs, was not
  tried. Losing field of view in the headset isn't acceptable.
- **Render Quality 1.25** makes LMU draw 1.56× the pixels. Not measured: the GPU load or frame rate
  while driving.
- **LMU restarts to take a new Render Quality.** It picks its render size at start.

## Native canvases on a Pimax Dream Air in LMU

The root `README.md` ("Headsets and games") suggests one canvas per source from these.
- **Source pixels per unit of view tangent:** the Pimax game eye about 1059 at Render Quality 1.0
  (2508×2084) and 1323 at 1.25 (3134×2604). The OpenXR source's image about 2115 at 1.0 (5084×4168).
- **The highest zoom at which a 16:9 canvas is native,** and the room at that zoom, worked out from
  those and the Dream Air's eye tangents (`findings\mirror_app.md`, "Zoom"):

| Source | Render Quality | Canvas | Max zoom for native | Stabilization room at that zoom (side to side) |
|---|---|---|---|---|
| Pimax Mirror | 1.0 | 1920x1080 | 1.31 | about 7.7° each way |
| Pimax Mirror | 1.0 | 1660x930 (soft once upscaled) | 1.5 | about 11.5° each way |
| Pimax Mirror | 1.25 | 1920x1080 | 1.63 | about 13.8° each way |
| Pimax Mirror | 1.25 | 2400x1350 | 1.31 | about 7.7° each way |
| Pimax Mirror | 1.25 | 2560x1440 | 1.22 | about 5.7° each way |
| OpenXR Mirror | 1.0 | 2560x1440 | 1.98 | about 18.9° each way |
| OpenXR Mirror | 1.0 | 3840x2160 | 1.32 | about 7.9° each way |

- **At Render Quality 1.0, 1440p and 4K can't be native from the Pimax game eye.** Even zoom 1.0, with
  no room at all, gives only about 2480x1400 of source.
- **The OpenXR source's stored size:** at Render Quality 1.0 a 4K canvas at zoom 1.25 shows the game's
  image at 95% of its size, so the source keeps it whole, and 55 images fit in 4.7 GB. A 1440p canvas
  shows it at 63%, so the source stores it at that size. At Render Quality 1.25, 55 images no longer
  fit in 4.7 GB, and each is stored at about 80% of its size. The OBS log showed "frame ring:
  5084x4168" with Look-ahead on a 4K canvas at zoom 1.25. The other sizes are worked out.
- **Video memory per copy:** 84.8 MB from the OpenXR source at the game's own size. From the Pimax
  source 21 MB at Render Quality 1.0 and 33 MB at 1.25, so 4 copies take 84 and 130 MB instead of 167
  and 261 MB. The OpenXR layer's three textures take 85 MB each, 254 MB.
- **The view at zoom 1.25** is about 87° wide, with about 6.4° of room each way, in both sources. It
  looks 2.3° (Pimax) to 2.5° (OpenXR) outward of straight ahead, because the left eye image reaches
  further outward than inward.
- **How low a zoom stays calm, with Look-ahead:** on real head motion, the edge is never reached at
  zoom 1.29, and touched on about one frame in a thousand at 1.2. At 1.1 the view gets shakier than
  with Instant. So about 1.2 is the lowest zoom that stays calm. These were measured with the Pimax
  source, with 0.5° more room on the view's outer side than it has now, and not measured again.
- **Audio:** with Look-ahead, LMU's sound shows about +415 ms and the microphone about +535 ms ("audio
  sync: 2 audio sources now +535 ms on top of their own offsets").

## Diagnostics

- **The game's frame rate in the 10 s stats is over the same 10 s.** Until October 2026 it was over
  the last 3 s only, so a stretch with a game stall in its first seconds read "90 fps" beside hundreds
  of repeats.
- **At a steady 90 fps the repeats match the frames the game never rendered.** In two race
  traces: 1,506 repeats against 1,648 missing frames. OBS's clock against the headset's adds about
  one skip every 1000 s (the data folder's resync check).
- **CPU is counted in cycles,** because `GetThreadTimes` charges whole 15.6 ms timer ticks and can't
  see a thread that runs a few microseconds at a time. Checked: a thread spinning flat out reads
  99.9%.
- **The frame ring keeps 4 copies** once the delay is measured. "held the last image (copy late)" was
  0 in every log with 8.

### Footprint without a game

Measured with Pimax Play running, the headset connected but not worn, no game, the source
shown, 10 s stretches. "Before" is the build before the idle state, with the footprint counters
added to it. "Idle" is the first build with the idle back-off.

| Measure | Before (no idle state) | Idle |
|---|---|---|
| Poll thread | 2.9–3.0% of one core, 1,830–1,890 wake-ups/s | 0.02%, 20 wake-ups/s |
| Copy thread | 0.01–0.08%, 19–20 wake-ups/s | The same |
| LMU telemetry thread (LMU not running) | 0.00%, 10 wake-ups/s | The same. Since November 2026 it is the telemetry reader, at 1 wake-up/s from the code |
| Graphics thread per tick (no frame to draw) | p99 0.002 ms, longest 0.011 ms | p99 0.002–0.003 ms, longest 0.005 ms |
| Pimax calls | 2,400–2,560/s, 82–88 ms of call time per 10 s (below) | 5/s, under 3 ms per 10 s: `status` 4/s, `config` 1/s |
| `pi_server` CPU (its process CPU time over 20 s) | 63% and 69% of one core without OBS, 76% with the source shown | 72% without OBS, 63% with the source shown |

- Before, the Pimax calls were `clock`, `pose` and `slot` at 770–820/s each (average 27–39 µs,
  longest 350–490 µs), `prediction` at 87–90/s, `status` at 4/s and `config` at 1/s.
- Before, the poll thread woke about 1,860 times a second, not the 4,000 its 0.25 ms timer asks for.
  Each pass takes its own time, mostly waiting on the three Pimax calls.
- It called Pimax about 2,500 times a second with no game, and each call waits 30–40 µs for
  `pi_server`. That cost `pi_server` roughly 7–13 points of one core.
- Idle, the source's cost to `pi_server` is within `pi_server`'s own variation (63–72% on its own).
- Before the idle state, the source logged "low mirror frame rate" every 3 s while no game ran.
- Not measured: the same during a race, and OBS's rendering-lag count with the source shown and
  hidden.

## The game behind, and the drawn pose stage

- **Without the slot check,** frames the game drew for an earlier slot made the image jump by 4–6 px
  at 1080p while the head moved.
- **With Smooth and Smoothest** a frame's slot is decided with the frames after it too. In e2e's
  replays of real races (`findings\e2e.md`, "Smoothing compared with Instant: what it found"), that
  took the frames warped from the wrong pose:
  - where the cockpit is dark and featureless: to 0.1–0.2% (Instant: 1.3–1.4%)
  - in a dust-cloud queue: to 4.4–8.5% (Instant: 6.9–9.7%)
- **The cockpit correction** (`drawn_pose` 2) is the plugin's choice.
  - In e2e, with the game at 82 fps and a detailed cockpit, it took the image's visible jumps from
    12.7–29.3% of frames to 1.1–1.2% (`findings\e2e.md`, "The cockpit correction").
  - As the default it failed all four real-race replays. Visible jumps: 14.7–19.1% in
    the dust cloud against 0.8–1.8%, and 11.8–12.3% in the dark cockpit against 0–1.9%. Their 64×32
    thumbnails are too blurry to judge it, but it didn't fall back on them either.
  - So the plugin uses it by choice, to be judged in real recordings. The mirror app and e2e keep the
    slot check as their default, which the replays can judge.
  - Its time: 0.36 ms of the copy thread per frame with patches, in e2e.
  - Not checked in a real recording.
- **Off** (`drawn_pose` 0) made more visible jumps in e2e's dust-cloud replay, not fewer: 5.0–5.6%
  of frames against 0.8–1.8% (`findings\e2e.md`, "Tried and dropped: poses from the arrival timing
  alone"). It is kept for comparing in a recording.

## OBS's plugin manifest

What OBS does with the plugin's `manifest.json`. All of it holds for Overlays' manifest too.

- **OBS 32.2.2 reads it** from the plugin's data folder when it loads the plugin, and writes no log
  line about it (`obs_module_load_metadata` in
  [libobs/obs-module.c](https://github.com/obsproject/obs-studio/blob/32.2.2/libobs/obs-module.c)).
  Checked with the local files test's private OBS, its `OBS_PLUGINS_DATA_PATH` naming a
  folder with both manifests. The Plugin Manager's record (`modules.json` in the `plugin_manager`
  folder of OBS's config) then held each plugin's `id` (`simoptix_vr_mirror`, `simoptix_overlays`) and
  `version` (`0.1.0+14.g31637d5`). Without the manifests both were empty.
- **The Plugin Manager's dialog (Tools → Plugin Manager) shows only `display_name`.** Without a
  manifest it shows the plugin's `obs_module_name`, or else the DLL's name without its extension. It
  shows no version. This is from OBS 32.2.2's source
  ([PluginManagerWindow.cpp](https://github.com/obsproject/obs-studio/blob/32.2.2/frontend/plugin-manager/PluginManagerWindow.cpp)),
  not looked at on screen.
- **A manifest without `display_name` shows an empty name.** OBS reads every key as a string without
  checking that it is there.
- **OBS 32.2.2 keeps the other keys but doesn't use them:** `name`, `description`,
  `long_description`, `os_arch` and `urls`. OBS documents none of the keys, so `os_arch` holds
  `windows-x64` as a guess.
- **Enabling and disabling** in the Plugin Manager is remembered by the DLL's name without its
  extension, not by `id`.
