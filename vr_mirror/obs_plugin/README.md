# SimOptix: VR Mirror (OBS plugin)

This plugin adds two OBS sources that show the stabilized headset view, and race auto-record, which
makes OBS record each LMU race by itself. Each source draws the view itself on OBS's own frame clock.
There is no window to capture, and no mismatch between the game's clock and OBS's. The plugin is
built against OBS 32.2.2 as `simoptix_vr_mirror.dll`.

The two sources show the same view. They differ in where the game's image comes from:
- **SimOptix: Pimax Mirror** stabilizes Pimax's client mirror, a half-size copy of the game's left eye
  that the Pimax runtime makes for other programs (`vr_mirror\frame_sources\pimax\`). It needs Pimax
  Play.
- **SimOptix: OpenXR Mirror** stabilizes the game's own left eye at full size, with the exact head pose
  each frame was drawn with, from the API layer in the game (`vr_mirror\frame_sources\openxr\`). It
  needs the layer installed. Unsigned, the layer needs LMU started without Easy Anti-Cheat, which
  means offline. Signed with a code signing certificate Windows trusts, it loads in LMU started the
  usual way (the layer's README says more). It uses no Pimax software.

## Terms

| Term | Meaning |
|---|---|
| client mirror | A feature of the Pimax runtime. It gives other programs a half-size copy of the left eye image the game draws, and a signal for each new frame |
| game eye | The eye image a source works from. The Pimax source's is the client mirror's half-size copy of the left eye. The OpenXR source's is the left eye at full size |
| output delay | How far the view runs behind the game: about 0.5 s with Look-ahead, none with Instant, and 0 while the source is hidden |
| display slot | One of the regular moments at which the headset shows a new image, one per refresh (90 per second at 90 Hz). The game draws each frame for one slot, with the head pose the runtime predicted for that slot |
| game behind | The game's GPU has fallen behind and queues up to two frames. Each arrives one or two display slots after the slot it was drawn for |
| slot check | How the Pimax source finds the slot a late frame was really drawn for: it compares small thumbnails of consecutive frames |
| cockpit correction | A refinement of the slot check. It measures how the fixed cockpit moved between two frames, and corrects the pose by what the head poses don't explain |
| look-ahead | The method behind the Look-ahead smoothing. The view runs half a second behind the game, and the smoothing plans a calm path from the head's movement in that time |
| drawn pose | The head pose a game frame was drawn with. The pipeline's second stage finds it, and the frame trace records it |
| tick | One frame of OBS's output. The OBS log counts them as "output ticks" |
| mirror app | `simoptix_vr_mirror.exe`, VR Mirror in a desktop window, for testing (`vr_mirror\mirror_app\README.md`) |
| observer | The debugging code in VR Mirror that numbers the stamp and writes the frame trace |
| stamp | The frame number drawn as 15 small squares in the top-left corner of the view, when Debug is on |
| frame trace | The CSV file Debug writes, with one row per drawn frame: its poses and timing |

The G-meter comes from a separate plugin, Overlays (`overlays\README.md`). When both are installed,
they share two things through memory that each opens on its own:
- the output delay of the VR Mirror source on screen (`shared\program_delay.h`)
- the game's telemetry, read once for both and handed out already matched to the moment the view
  shows (`shared\telemetry\README.md`)

So the G-meter, race auto-record and the audio all follow the moment the view shows.

## Install

1. With OBS closed, run `build.bat install` in this folder. The root `README.md` lists what the build
   needs. The script builds the plugin and copies it to
   `C:\ProgramData\obs-studio\plugins\simoptix_vr_mirror\bin\64bit`. It also copies the plugin's
   `manifest.json` to `data` beside `bin`. OBS 32 reads the plugin's name and version from it. If you use the Overlays plugin,
   install it from the same build.
2. In OBS, add a **SimOptix: Pimax Mirror** or **SimOptix: OpenXR Mirror** source. For the OpenXR one,
   install the API layer too: close LMU and run `vr_mirror\frame_sources\openxr\install.bat`, which
   asks for administrator rights. The dev skill's `reference\openxr.md` has the steps.

Reading LMU needs its telemetry headers from the game's folder at build time, because their licence
doesn't allow copying them into this repo. The build finds the game in any Steam library. A build
without the headers says so and reads no game, and race auto-record then records nothing.

A source runs only while it is shown. Hidden (eye icon), it has no threads. The Pimax source then has
no Pimax session, and the OpenXR source doesn't read the layer, so the layer copies no frames.

The Pimax source takes about 0.4 s to connect and up to about 0.3 s to disconnect. The OpenXR source's
times were not measured. In both sources, connecting and disconnecting happen on a thread of their
own, so OBS's output never stops for them. The OBS log says "Pimax session ready after ... ms" (or
"OpenXR layer ready after ... ms") and "stopping (source hidden): graphics thread done in ... ms".

Crash dumps of either plugin go to `%LOCALAPPDATA%\simoptix`, at most two per OBS session. The newest
10 crash dumps are kept. When OBS starts, the plugins delete the oldest beyond 8, so that session's two
still fit. The OBS log says how many they deleted. The OBS log marks this plugin's lines
`[simoptix_vr_mirror]`.

The plugin writes its version to the OBS log when OBS loads it
(`[simoptix_vr_mirror] loaded, version 0.1.0`). Windows shows the same version in the DLL's
Properties, under Details. A build of a release shows
its number (`0.1.0`). Any other build adds the commits since that release and the commit
(`0.1.0+12.gabc1234`).

## What the sources show

**SimOptix: Pimax Mirror** shows the Pimax game eye, a half-size copy of the left eye the game
renders. It grows with Render Quality ("Recording at the source's native resolution", below). It
arrives with every game frame and costs almost nothing (`vr_mirror\frame_sources\pimax\README.md`).
The source keeps the image at its own size. Each frame's pose is Pimax's prediction for the slot the
frame arrived for, and the slot check finds the slot a late frame was really drawn for ("When the game
falls behind", below).

**SimOptix: OpenXR Mirror** shows the left eye the game itself draws, at full size. The layer
copies it after each frame has gone to the headset, and the copy takes about 0.2 ms of the game's
graphics card (`vr_mirror\frame_sources\openxr\README.md`). The source keeps the image at the size the
canvas shows it (below). Each frame says the moment it was drawn for and the pose it was drawn with, so
its pose is exact and needs no slot check.

## Settings

Both sources have the same four settings:

| Setting | What it does |
|---|---|
| **Smoothing** | A list that sets how the camera follows the head: Off, Instant or Look-ahead (below). Look-ahead is the default |
| **Zoom** | A slider from 1 to 3, 1.25 by default. The source shows the middle 1/zoom of the eye image, and the image around it is the room the stabilization uses (below). The same zoom gives the same view in both sources |
| **Record races automatically** | OBS records each race by itself, from the start lights to 5 s after the car stops past the finish ("Recording races automatically", below). Off by default |
| **Debug** | Stamps frame numbers on the view and writes a frame trace, for checking a recording frame by frame ("Debug: stamp and frame trace", below) |

The source is always the size of the OBS canvas (Settings → Video → Base resolution), so it fills the
canvas at 1:1. When the canvas changes, the source follows at once.

Everything else is fixed in the code (`plugin.cpp`, the constants above `MirrorSettingsOf`, and the
core's own):
- The horizon is kept level.
- The filtering is sharp (bicubic).
- Each frame is warped from the pose the game drew it with.
- The Pimax source runs the slot check while the game is behind ("When the game falls behind", below).
- The OpenXR source takes the slot and the pose each frame reports.
- Instant's filter has a 0.5 Hz cutoff, beta 2 and a 0.5° dead zone.

### Smoothing

- **Look-ahead** (the default): the view runs about half a second behind the game. The source uses
  that half second to see where the head goes next, so it follows each look closely. It removes tremor
  and the headset's tracking noise completely, without the floaty lag of a filter that can't see ahead.
  For recording.
- **Instant:** no delay. A filter that has to trail the head to stay smooth. For live streaming,
  where every bit of delay counts.
- **Off:** no stabilization. The view is the headset's view as it is, with no delay. Switching
  Smoothing on or off starts the smoothing afresh.

On 4.3 h of real head poses, Look-ahead's view trails the head by 0.95° at most 99% of the time
(Instant: 4.6°), and what tremor and tracking noise leave is under a tenth of a pixel. This was not
checked in a recording.

**The delay costs video memory,** because the source keeps a copy of every game frame within it. With
the Pimax source that is about 1.1 GB at Render Quality 1.0, and about 1.7 GB at 1.25.

About 2 s after the first game frame, the OBS log says the delay the source settled on: "output delay
... ms (look-ahead 530 ms)", then "copy delay fixed: ... ms, ... ms copy ready p95".

**What follows the output delay by itself:**
- The **G-Meter Overlay** shows what the car was doing at the moment of the game that the view shows.
  It works that moment out from the delay, not by looking at the image.
- **Record races automatically** starts and stops by that same moment. So the recording starts at the
  start lights and ends after the finish, as the recorded video shows them.
- **The sound.** While the source is shown, the plugin adds the delay to every audio source's own Sync
  Offset (Edit → Advanced Audio Properties), and takes it off again when the source is hidden.
  - Keep each audio source's own offset as it is. A game whose sound trails its image needs an offset
    of its own. LMU's sound trails by about 120 ms, so it needs about −120 ms. With Look-ahead, LMU's
    sound then shows about +415 ms and the microphone about +535 ms.
  - The OBS log says so: "audio sync: 2 audio sources now +535 ms on top of their own offsets".
  - Each audio source remembers the shift it got, saved with the scene. So its own offset comes back
    exactly, also after OBS was closed with the source shown.
  - Changing an offset while it is shifted changes the source's own offset by as much.

### Zoom

More zoom means a calmer camera on big head movements. Less zoom means more detail and a wider view.

The view looks at the middle of the eye image, halfway between its edges, so it has the same room on
each side. On a Pimax Dream Air in LMU, at zoom 1.25 it is about 87° wide, with about 6.4° of room
each way, in both sources. There the view looks 2.3° (Pimax) to 2.5° (OpenXR) outward of straight
ahead, because the left eye image reaches further outward than inward.

- With Look-ahead, a lower zoom costs less calm. The source sees each edge coming in the time
  it runs behind, and eases the view towards the head ahead of time instead of stopping it at the
  edge.
- With Instant, the view catches up faster near the edge, and stops at it as a last resort.
- On real head motion, the edge is never reached at zoom 1.29, and touched on about one frame in a
  thousand at 1.2. At 1.1 the view gets shakier than with Instant. So don't go below about 1.2 with
  Look-ahead. These were measured with the Pimax source on a Pimax Dream Air in LMU, with 0.5° more
  room on the view's
  outer side than it has now, and not measured again.
- The source draws on OBS's clock, so each OBS frame shows a game frame drawn for a slightly later
  moment, or the previous one again. The smoothing keeps the view inside that frame's image and plans
  ahead for it.

## Recording races automatically

Tick **Record races automatically** in a VR Mirror source's properties, and OBS records each LMU race
by itself. It is off by default.

- **Races only:** LMU's race sessions, not practice, qualifying or warm-up.
- **The recording starts when the start lights come on** (LMU's "start-light countdown" phase).
  - On a rolling start, that is the end of the formation lap, when "hold speed" shows. The formation
    lap itself isn't recorded.
  - If LMU goes straight from the formation lap to green without the plugin seeing the countdown, the
    recording starts at green.
  - Joining a race that is already under way starts no recording.
- **It stops 5 s after the car stops past the finish line,** once LMU marks the player's race
  finished, retired or disqualified. Driving on after the flag (a cool-down lap) keeps it recording
  until the car stands still for 5 s. A pit stop during the race doesn't stop it.
- **It also stops:**
  - 5 s after the player leaves the race (back to the monitor, no car, LMU closed)
  - 35 s after LMU stops sending updates (its menus send none)
  - straight away when a new session starts: the next session, or the race restarted. A restarted
    race is recorded again from its lights.
- **Recordings started by hand are left alone.** If OBS is already recording when the lights come on,
  the plugin starts and stops nothing in that race. If you stop the plugin's recording by hand, it
  starts again only at the next race.
- **It follows the output delay.** The view runs behind the game, so every timing above is counted on
  the moment of the game that the view shows, worked out from the delay this source publishes. With
  Look-ahead the recording starts about half a second after the game's start lights, so the recorded video begins
  at the lights. Auto-record checks four times a second, and OBS takes a moment to start recording, so
  the first 0.1–0.35 s of the lights are not in the recording. With the source hidden, the delay is 0.
- **It works whenever a VR Mirror source of either kind is in the scene collection,** shown or not.
  With several, it is on while any of them has it ticked.

Race auto-record reads LMU's shared memory, the official interface for outside tools, through the
telemetry both plugins share (`shared\telemetry\README.md`). One reader serves the whole of OBS, and
runs only while something needs it: a G-meter on screen, or race auto-record switched on.

The OBS log shows each step, in lines that start "LMU race auto-record:":
- on and off
- every change of LMU's session and phase, for example "session race, phase start-light countdown"
- "starting the recording", "recording the race" and "stopping the recording", with the reason

The decision is in `race_auto_record\race_recorder.cpp`, which is logic only and makes no OBS calls.
`race_auto_record\auto_record.cpp` checks it four times a second of the view's time on OBS's graphics
thread, never waiting for the telemetry reader. It starts and stops the recording through OBS's
frontend on the UI thread.

## Recording at the source's native resolution

Record at the source's **native** resolution, one recorded pixel per source pixel, and upscale once,
at the end. Every scaling step in between softens the image. Set OBS's frame rate to the headset's
refresh rate, which you choose in the headset's software, such as Pimax Play. While the game keeps up,
it draws one frame per refresh, so the source shows every game frame exactly once and motion is even.

The canvas that is native depends on the headset, its Render Quality and the zoom. The root
`README.md` ("Headsets and games") suggests a canvas for each measured headset. On a Pimax Dream Air
in LMU, the Pimax game eye has about 1059 source pixels per unit of view tangent at Render Quality 1.0
and 1323 at 1.25. The OpenXR source's image has about 2115 at 1.0. So a 16:9 canvas there is native up
to these zooms:

| Source | Render Quality | Canvas | Native up to zoom | Stabilization room at that zoom (side to side) |
|---|---|---|---|---|
| Pimax Mirror | 1.0 | 1920x1080 | 1.31 | about 7.7° each way |
| Pimax Mirror | 1.0 | 1660x930 (soft once upscaled) | 1.5 | about 11.5° each way |
| Pimax Mirror | 1.25 | 1920x1080 | 1.63 | about 13.8° each way |
| Pimax Mirror | 1.25 | 2400x1350 | 1.31 | about 7.7° each way |
| Pimax Mirror | 1.25 | 2560x1440 | 1.22 | about 5.7° each way |
| OpenXR Mirror | 1.0 | 2560x1440 | 1.98 | about 18.9° each way |
| OpenXR Mirror | 1.0 | 3840x2160 | 1.32 | about 7.9° each way |

The zooms and the room are worked out from the measured image sizes and the headset's field of view.
At Render Quality 1.0, 1440p and 4K can't be native from the Pimax game eye. Even zoom 1.0, with no
room at all, gives only about 2480x1400 of source.

**The OpenXR source** keeps each frame's image at the game's own size when the canvas shows it at 83%
of that size or more. Otherwise it keeps the image at the size the canvas shows it 1:1. Each
resampling softens the finest detail, so near the game's size the view resamples the image once,
straight from the game's.
- All stored images together may take at most 4.7 GB of video memory. Look-ahead stores 52 to 55
  images.
- On a Pimax Dream Air in LMU at Render Quality 1.0, a 4K canvas at zoom 1.25 shows the game's image
  at 95% of its size, so the source keeps it whole. 55 images fit in 4.7 GB at that size. A 1440p
  canvas shows it at 63%, so the source stores it at that size.
- A higher Render Quality makes the game's image larger than 55 of them fit in 4.7 GB. On the Dream
  Air at 1.25, each is then stored at about 80% of its size.
- The OBS log showed "frame ring: 5084x4168" with Look-ahead on a 4K canvas at zoom 1.25. The other
  sizes are worked out, not measured.

**For more detail from the Pimax game eye, raise Render Quality in Pimax Play.** The game eye is always
half the size of the eye image Pimax asks the game to draw, with no cap. The price is the game's: it
draws 1.56× the pixels at 1.25. Restart the game after changing Render Quality if it picks its render
size at start, as LMU does. The Pimax runtime has no size, crop or field-of-view option for its
mirrors, so Render Quality is the only way to a larger game eye.

## Diagnostics

### The OBS log

Every 10 s while a source is shown, also while no game runs, the OBS log (Help → Log Files) shows:
- how many output ticks showed a new game frame, a repeat, or held the last image
- the game's frame rate over the same 10 s. When OBS's frame rate is the headset's refresh rate, the
  repeats match the frames the game never rendered.
- the footprint ("footprint: ..."):
  - how much CPU the source's own threads use, as a percentage of one core, and how often each wakes
    up. They are the poll thread, which watches the frame signal and samples head poses, and the copy
    thread.
  - how long OBS's graphics thread spends each tick choosing the frame and computing the warp (99th
    percentile and longest)
  - The plugin that runs the telemetry reader logs the reader's footprint every 10 s while it is
    connected to a game, with that plugin's tag ("footprint: telemetry thread (Le Mans Ultimate) ...").
- for the OpenXR source, the frames read and the frames that were gone before they were copied
  ("OpenXR frames: ..."). The layer logs its own side, including the copy's time on the graphics card
  (`vr_mirror\frame_sources\openxr\README.md`).
- for the Pimax source, every call into the Pimax runtime ("Pimax calls: ..."), by kind, per second,
  with its average and longest time. Each is a call into Pimax's compositor (`pi_server`), so this is
  the load the source puts on it.
  - `clock`, `pose` and `slot` are the poll thread's samples.
  - `prediction` is the pose asked for each new display slot.
  - `status` is the check whether Pimax wants the session closed.
  - `config` is the mirror's handles, which the copy thread asks for every 2 s.
  - `now` is the clock asked before the source has synced it (the first moment after starting). It
    should stay near 0.

  Only the source's own threads call Pimax, under a lock of their own. OBS's graphics thread reads
  the frames and head poses under another lock, held only to copy them. So a slow Pimax call can't
  hold up OBS's output.

CPU is counted in cycles (`QueryThreadCycleTime` against the processor's timestamp counter), because
`GetThreadTimes` can't see a thread that runs a few microseconds at a time.

### Video memory

The source copies each game frame into a ring of textures as soon as it arrives, because the game
overwrites its image about a frame later. It shows the copy a fixed delay later (the OBS log: "output
delay ... ms", about 4–5 ms measured). The frame picker (`vr_mirror\core\pipeline\3_frame_picker\`,
`FixedDelayPicker`) decides which copy each tick shows, the delay, and the copies needed.
- The ring starts with 8 copies, because the source allows 40 ms until it has measured that delay. It
  then keeps only the frames inside the delay plus 3, and at least 4.
- From the OpenXR source each copy is the game's own size, or the size the canvas shows it (above).
  From the Pimax source each copy is the size of the game eye. A copy takes 4 bytes per pixel.
- On a Pimax Dream Air in LMU, an OpenXR copy at the game's own size takes 84.8 MB. A Pimax copy takes
  21 MB at Render Quality 1.0 and 33 MB at 1.25. So 4 Pimax copies take 84 and 130 MB instead of 167
  and 261 MB.
- The log says "frame ring: ... 8 slots, ... MB" when the first frame arrives, and "frame ring: 4
  slots, ... MB" about 2 s later.
- If a copy is ever too late for its frame, the 10 s stats count it as "held the last image (copy
  late)". That number should stay 0.

### While no game runs

The source idles.
- The OpenXR source looks for the layer's memory once a second. Otherwise it sleeps until the layer
  says a frame is copied. After 2 s without a game frame (none yet, or the game quit), it goes idle.
  The OBS log says so once each way: "OpenXR: no game frames for 2 s: idle" and "OpenXR: game frames".
- After 2 s without a game frame (none yet, or the game quit), the Pimax source stops asking Pimax for
  head poses, the time and the display slots, and only waits for the game's frame signal. It is back at
  full rate on the game's first frame, and catches that frame (e2e scenario `game_starts_late`). The
  OBS log says so once each way: "no game frames for 2 s: idle ..." and "game frames again after ... s
  idle: back to full rate".
- With Debug on, the frame trace starts at the game's first frame ("Debug: stamp and frame trace",
  below).
- The Pimax session itself stays open. The status check, 4 times a second, is how Pimax tells the
  source that the game has taken over. While idle, the source's cost to `pi_server` is within
  `pi_server`'s own variation.

### When the game falls behind

When the game falls behind (dust clouds, heavy traffic), it can queue up to two frames. Each arrives
one or two display slots after the one it was drawn for.

- **The OpenXR source's frames say which slot that was,** so the source takes that slot's pose.
- **The Pimax source can't know, so it runs the slot check.** It judges each frame's image to find the
  slot the frame was really drawn for, and warps the frame from that slot's head pose. It does this
  only while the frame timing looks disturbed. Otherwise nothing changes.

Every 10 s the log then says how many frames were judged, and how many had been drawn for an earlier
slot ("game behind: X frames rendered for an earlier slot ..., Y frames judged by picture"). Every
stage of the pipeline reports its own numbers this way, so a new method's numbers show up without
changes here.

With Look-ahead a frame is shown about half a second after it arrives, so its slot is decided with
the frames after it too. With Instant each frame is decided from the frames before it only.

The method is fixed in the code (`kDrawnPose`, the setting `drawn_pose` of
`vr_mirror\core\common\settings.h`). With Smoothing on, it is the slot check plus the cockpit
correction (2). With Smoothing off, it is off (0). The mirror app reads the setting from its ini file,
where 1, the slot check alone, is the default.

- **1, slot check:** as above.
- **2, slot check and cockpit correction:** the slot check, and while the game's frames arrive
  irregularly (dropped frames, bunching, slot jumps), each frame's pose is also corrected by how far
  its fixed cockpit (roof, pillars, dashboard) moved against the frame before, beyond what the head
  poses explain.
  - From frame to frame the image decides, and in the long run the head poses do. The correction is
    pulled back towards them over 0.3 s, so it can't drift.
  - It fades in and out over 0.3 s, and goes after half a second of the game keeping up with the
    headset. While the game keeps up, it does nothing extra.
  - Where the cockpit is too plain to measure, the frame keeps the slot check's pose
    (`vr_mirror\core\pipeline\2_drawn_pose\README.md`, "The cockpit correction").
  - Every 10 s the log says how many frames it corrected, how many it measured from the image, how
    many couldn't be measured, and the largest correction ("cockpit correction: ..."). It also gives
    the copy thread's time on frames with patches ("looking at pictures with patches: ... frames, ...
    us avg, ... us max").
- **0, off:** every frame is warped from the slot its arrival time gives it, and no images are judged.

### Debug: stamp and frame trace

For checking a recording frame by frame, the **Debug** setting does two things:
- It draws the stamp, the frame number as 15 small squares in the top-left corner of the view.
- While the source is shown, from the game's first frame, it writes two files to
  `%LOCALAPPDATA%\simoptix\traces`:
  - `trace_<time>_ticks.csv`, the frame trace, with one row per drawn frame:
    - the stamped number, the game frame shown, and the display slot it arrived for (`slot`)
    - the head poses the warp used
    - `img`, the pose the frame was drawn with as the drawn pose stage found it, and
      `drawn_pose`, how it was found: `slot_check`, `arrival_slot` or `cockpit`
    - timing and counters
    - the game frame the view followed, which differs from the frame shown on a repeated frame
    - how often and how far the smoothing had to stop the view at the edge of the image, as a last
      resort
    - the output's shape
    - the slot check's own notes on each frame, for example how many slots back it judged the frame,
      and how far the cockpit correction turned it
    - every 10 s, the stages' log lines, as `#stats,` rows

    The pipeline's observer writes it, and `vr_mirror\core\pipeline\observer\README.md` has the
    columns. `vr_mirror\recording_analysis` finds the slot a frame was drawn for by comparing `img`
    with the predictions.
  - `trace_<time>_pimax.csv`, from the Pimax source only: head poses at 1 kHz, the pose Pimax predicted
    for each display slot, and every game frame's arrival.
  - `trace_<time>_openxr.csv`, from the OpenXR source only: every frame the OpenXR layer published,
    with which of the game's swapchain images it showed and on which of the game's threads it was
    handed over, and when its copy arrived (`vr_mirror\frame_sources\openxr\README.md`, "The reader").

Turn Debug on and record, and the recording can be matched to the poses frame by frame. Leave it off
otherwise. The trace takes about 300 MB per hour with the Pimax source. With the OpenXR source it takes
about 100 MB per hour, and its own file adds about 30 MB (estimated from its rows' size, not
measured). It is written to disk on its own thread, so a slow disk never holds up OBS or the frame
signal. If the disk falls a minute behind, rows are dropped, and the OBS log says how many.

The newest 10 traces are kept. When a trace starts, VR Mirror deletes the oldest beyond 10, each with
all its files. The OBS log says how many it deleted, in the "frame trace:" line.
