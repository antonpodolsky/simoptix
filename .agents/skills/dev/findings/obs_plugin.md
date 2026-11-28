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
- **Out of step while driving, before auto sync** (#8). One recording, OpenXR Mirror on Look-ahead
  (544 ms delay), LMU's sound at −120 ms of its own. LMU's HUD dial is drawn into the game's image, so
  its pedal arc and gear number show what the view shows. They were compared frame by frame with the
  G-meter's pedal bars and with the engine's sound. "Ahead" is earlier in the video than the view:

  | What | Against | Result | Events |
  |---|---|---|---|
  | G-meter throttle (telemetry) | HUD throttle | 37 ms ahead (middle half 33–42 ms) | 12 lifts, 3 presses |
  | G-meter brake (telemetry) | HUD brake | about 45 ms ahead, a weak measurement | the whole recording |
  | Engine sound | Telemetry throttle | 47 ms ahead (middle half 33–52 ms) | 11 lifts |
  | Engine sound | HUD throttle | 85 ms ahead (middle half 68–91 ms) | 9 lifts |
  | Engine sound at upshifts | HUD gear number | 73 ms ahead (middle half 43–86 ms) | 29 upshifts |

  - The G-meter is early because the delay counts from a frame's arrival, while the telemetry is
    stamped as soon as LMU publishes a physics step. The game draws the frame after that step.
  - The −120 ms offset was about 80 ms too much there. So one fixed offset doesn't fit every frame
    source and setup, and auto sync measures it instead.
- **Tried and dropped: the telemetry delay measured at LMU's upshifts.** Auto sync timed each upshift in
  the telemetry against the gear number changing in the game's image, to put the G-meter and race
  auto-record in step with the view. #72 looks for that delay in the frames' own times instead, which
  needs no driving and no image.
  - LMU's HUD shows N between gears: in a second recording (OpenXR Mirror on Look-ahead), 24 of 25 gear
    changes in 2 minutes went through N, for 3 frames or 6. The G-meter's throttle ran 41 ms ahead of the
    HUD's there (6 lifts and presses, middle half 34 to 43 ms), as in the table.
  - On made-up laps with known lags and no gear display, the best spot in the image stepped 44 grey levels
    at the first upshift and about 20 after, and gave telemetry delays near 311 ms. A ramp of scenery whose
    change grew over 10 frames as the car turned faster gave 283 ms. Asking the change to stand out of the
    frames either side of it rejects it.
  - On a race's frames around 18 upshifts (OpenXR Mirror on Look-ahead, in 4K, 384 pixels wide, about
    0.27° a pixel), with the cameras rebuilt from the trace, the dial's gear number stepped 20 to 28 grey
    levels an upshift, and its change jumped 1300 to 2800 against 30 to 200 in the other frames.
  - At the plugin's grey copies' resolution (256 texels over 80% of the eye image's width, about 0.43°
    each), the digit's strokes blur, and the dial stepped 10 to 12 grey levels an upshift. The scenery
    through the windscreen stepped 14 to 20 at the same upshifts, at a steady lag too (the car's jolt).
    Only cells whose biggest change stood out 4 times of the frames either side counted, as a digit
    changes in one frame and the jolt over a few: that timed 14 to 15 of the 18.
  - Delays of 40 and 80 ms put in gave 43 and 82 ms, then 44 and 82 ms, each from 5 of 7 upshifts. The
    change was found either at the delay put in or a frame period after it.
- **Tried and dropped: the engine's pitch drop at an upshift, for the game's sound.**
  - On the recording of the table above, the engine's harmonics (450–3500 Hz) dropped by about 16%
    within about 50 ms at every upshift. Downshifts were too noisy to use. Searched over any drop of 6%
    to 46%, a minute of a crowded race gave about 90 "drops", many as strong as real upshifts, so the
    search looked only near the drop that the telemetry's RPM gave (±4%).
  - On that race's sound, with its own sync offset (+416 ms) taken out, a drop was found at all 18
    upshifts, but spread widely: middle half about 71 to 170 ms, from 51 to 378 ms. The drop's size had
    to come from the sound itself, as a recording holds no RPM.
  - Weighing the frequencies near the engine's harmonics at the RPM before the drop made it depend on the
    RPM: on that race, one RPM for every upshift (7,875, the median the sound showed) settled the game's
    sound at -171 ms instead of -75 ms, and each upshift's own RPM from the sound at -90 ms.
  - Played through a speaker and captured as Desktop Audio in OBS, a made-up engine's pitch drops came
    from -61 to 375 ms after the telemetry, against 30 ms put in, and the game's sound never settled.
  - So the game's sound is measured at the hovers in LMU's menus instead.
- **The hovers in LMU's menus.** One recording of the menus (OpenXR Mirror on Look-ahead, a Quest 3,
  LMU's sound on Application Audio Capture at -120 ms of its own):
  - Most items tick and light up when the pointer comes onto them. 21 ticks rose 32 to 52 dB above the 40
    ms before them, in 2 ms frames of the sound's first difference, where the menu's music rose less than
    20 dB. The louder click of a page change came at other lags.
  - The highlight comes in one frame. On the main menu its box stepped 14 to 111 grey levels. In the race
    weekend's screens it stepped 1 to 2, too little to find without the tick. Looking for highlights first found
    about 6 real hovers of 14, at lags spread from 73 to 121 ms. Looking for the ticks first, then the
    frame before each in which a small part of the menu lit up, gave 106 to 119 ms at 5 hovers in a row.
  - In the recording, with LMU's -120 ms own offset, the ticks came 15 to 61 ms before their highlights
    (17 of the 21, middle half 20 to 42 ms): LMU's sound trailed its image by about 90 to 110 ms there.
  - That session's frame trace has the same head pose in every tick for 106 s, so the menu didn't move in
    the frames.
- **The auto sync test** (`vr_mirror\e2e\README.md`, "The auto sync test"):
  - The game's sound settled at 102 ms with the menus' sound as recorded and 166 ms with it 60 ms later
    (moved 64 ms), then at 98 and 167 ms (69 ms), 100 and 166 ms (66 ms), 106 and 166 ms (60 ms), 102 and
    165 ms (63 ms), and 99 and 159 ms (60 ms), each with 5 ticks agreeing. The highlight is timed to the frame (11 ms), so two runs can differ by about
    that much, close to the test's 10 ms for the move. Once it moved 71 ms (98 and 169 ms), and the test
    failed. The next run moved 60 ms (99 and 159 ms), with the same build.
  - In one run, the first measuring took 31 ticks, against 6 in the next run. On the way it moved the
    game's sound by 396 ms, from 3 ticks about 400 ms after a highlight, and still settled at 100 ms.
  - With other sound playing on the same output device, two runs in a row heard one tick each, rising 25
    dB where the menus' ticks rise 44 to 68, and never settled. With nothing else playing, the next run
    passed.
  - The menus start when auto sync logs that it measures, about 3 s after the game's frames start. Then
    the first measuring was done after 6 ticks, about 4 s, and the test took 29 s.
  - A highlight is drawn over two frames: the step was 30 to 40 grey levels in one frame and 40 to 42 in
    the next, against a median step of 0.1 to 0.2. The bigger one is taken.
  - Measuring again: after the game's frames stopped for 3 s and the menus played again, auto sync measured
    again and settled at 165 ms against 164 the first time, then 165 against 166, 166 against 166, and 159
    against 159.
    Read every 2 s, the frame source's idle state (no frame for 2 s) missed a 3 s pause. The frame ring
    sees every frame, and counts each that comes 2 s or more after the one before.
  - Each check was shown to fail when auto sync was broken: moving the game's sound at the first tick,
    without three that agree, moved it 80 ms first and 109 ms when it stopped, on the menu cut.
- **The game's own sync offset was added to auto sync's correction** (fixed in #80). Auto sync measures
  the sound before OBS applies any sync offset, but the plugin still added the game's own offset to the
  correction. In one OBS session (OpenXR Mirror on Look-ahead, 537 ms delay), LMU's sound still had the
  −120 ms offset set by hand before auto sync existed. Auto sync measured −120 ms, and later −134 ms. OBS
  then showed +283 ms (537 − 134 − 120), so the sound was about 120 ms early in both race recordings of
  that session. Since the fix, the game's sound gets only the delay and auto sync's correction. Its own
  offset is kept, and applies again when the source is hidden.
  - **Measured in those two race recordings** with `sync_check.py`, which times the engine's sound
    getting quieter at each throttle lift against the HUD's throttle arc. In race A the sound's sync
    offset was 297 ms (537 − 120 − 120), and the sound came 123 ms before the view (middle half 94 to
    145 ms, 14 lifts). So LMU's sound reached OBS about 117 ms after the image (middle half 95 to
    146 ms), close to the 120 and 134 ms that auto sync measured in the menus. With the fix, the sound
    would have come about 3 ms before the view. Race B (283 ms) had only 2 usable lifts, with the sound
    96 ms early. The G-meter's throttle was 38 and 40 ms ahead of the HUD's (88 and 60 lifts and
    presses), as in the table above.
  - The table above (#8) found LMU's sound about 35 ms after the image in a race. That recording had a
    different setup, and the two haven't been compared.
  - The auto sync test now starts Desktop Audio with a sync offset of −120 ms in its first run, and
    checks the sync offset that OBS saves. With the fix, OBS saved −95 ms: the 4 ms delay (Instant) plus
    the −99 ms correction. Without the fix it saved −216 ms (4 − 100 − 120), and the check failed.
  - In that session, the ticks that auto sync matched to a highlight came 66 to 141 ms after it, most of
    them 110 to 134 ms. Most ticks matched no highlight: with the head moving, the median change between
    frames around them was 3 to 18 grey levels, so the highlight didn't stand out.
- **Auto sync took a microphone for the game's sound** (fixed in #80). In the same session, one
  measuring picked Mic/Aux after 49 ticks, because 3 of them agreed at 391 ms, and moved Mic/Aux by
  −391 ms. A later measuring picked LMU's sound again. Since the fix, auto sync doesn't read audio input
  captures.
- **Not checked yet:** auto sync in the game. Whether a sync offset changed during a recording makes an
  audible glitch. Hovers with the head moving. Whether the menus' sound and a race's sound have the same
  lag. Measuring again on a change of frame rate, image size, OBS's audio sources or Windows' output
  device (only the restart of the game's frames is tested).

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

## Copying only the frames OBS shows

- **The OpenXR source with Look-ahead holds each frame for the next and copies only those shown**
  (`findings\pipeline.md`, "Copying only the frames shown"). Measured once in the local files test's
  private OBS (60 fps) with the fake OpenXR game (90 Hz) and Look-ahead set for that run:
  - The copy delay was fixed at 15.6 ms (copies ready at 13.6 ms, 95th percentile), where copying
    each frame as it arrives fixes 4.0 ms there: the wait for the next frame.
  - The ring went from 55 entries (466 MB) when the delay was fixed to 37 (313 MB) 0.6 s later.
  - Over the 119 ticks after that, the frame shown moved on by one and by two in turn (59 each), the
    90 Hz to 60 fps pattern: no repeat and no bigger jump.
  - Not checked: a real headset and game, OBS's 10 s stats over a long run, and Pimax, which copies
    every frame as before.
- **Tried and dropped: holding every frame for the next one.** In LMU, the OBS log's "images gone
  before they were copied" counted 21 of VR Mirror's in about 31,000 frames, against 0 in 766,093
  frames of the build before (which copied each frame as it arrived). The layer writes frame N's
  texture again with frame N+3, as the game ends that frame, and VR Mirror sees a frame arrive only
  once the layer's copy has finished on the GPU. So holding left a median of 11 ms (one display
  period) before the image was written over, against 22 ms when copying at arrival. Each loss came
  where the PC was busy: the game starting, a recording starting or stopping (switching between OBS
  and the game), a game hitch. One showed as a frame repeated for 0.12 s, then a jump.
  - On the OpenXR traces, a copy made after the frame's texture was written over, by the F rows'
    end times (`stage_replay --stage frame_picker`): holding every frame loses 60, 3, 12 and 13
    images in four traces (766,094, 15,176, 7,350 and 9,746 frames), copying at arrival none.
  - So a frame the forecast can tell is shown even with the next frame on time is copied at once,
    and only the others wait. That loses none in the four traces, and repeats as copying at arrival
    does. From where OBS ran at 60 fps in the last trace, with Look-ahead: 417 repeats (0 with a
    newer frame due) like copying at arrival, 30% of the frames not copied, a store of 37 entries
    instead of 54, and the copy delay back to 4.9 ms (16.5 ms when holding every frame).
- **In LMU with that build: the frames shown as when every frame was copied, a third of the copies
  skipped.** One session with the OpenXR source, Look-ahead, OBS at 60 fps (NVENC AV1, 4K), with a
  recording of 3,323 frames:
  - The log: the copy delay fixed at 6.5 ms, and the ring at 37 entries (3,136 MB) from 0.6 s after
    that, against 54 (4,577 MB) before. About 300 frames of 900 were not copied per 10 s, and no
    copy was late. One image was lost before it was copied, in the 10 s after the game started (at
    38 fps), and none in the rest of the session or the recording.
  - The recording, lined up with its trace: no game frame shown twice, none given a slot two or
    more on, no OBS tick shown twice or dropped, no cockpit jump over 2 px. 1,661 game frames never
    shown, a third, as at that rate.
  - The session's trace in `stage_replay`: 8,217 of 11,825 frames copied, 1,062 repeats (none with
    a newer frame due) and no image lost, as copying at arrival. Holding every frame there loses 5.

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

## Native canvases on a Meta Quest 3 in LMU

The root `README.md` ("Recording in OBS") suggests a canvas per setting from these. The headset ran on
Meta Horizon Link, and its setting is the Oculus Debug Tool's Pixels Per Display Pixel Override.
- **The eye image:** 2720×2976 at the default (0), as the headset test read it, and 4080×4448 at 1.5, as
  LMU drew it in one recording (the OBS log's "frame ring: 4080x4448"). Source pixels per unit of view
  tangent: about 1228 at the default and 1842 at 1.5.
- **The field of view of the left eye,** from the layer's log in LMU: 54° outward, 40° inward, 44° up
  and 55° down (`findings\openxr.md`, "LMU on a Meta Quest 3").
- **The highest zoom at which a 16:9 canvas is native,** worked out from those with the pipeline's
  `NativeImageWidth`, as the empty state does. The same calculation gives the Dream Air's rows above
  within 0.01, from the rounded tangents there.

| Pixels Per Display Pixel Override | Canvas | Max zoom for native |
|---|---|---|
| 0 (the default) | 1920x1080 | 1.37 |
| 0 (the default) | 2560x1440 | 1.02 |
| 1.5 | 1920x1080 | 2.05 |
| 1.5 | 2560x1440 | 1.54 |
| 1.5 | 3200x1800 | 1.23 |
| 1.5 | 3840x2160 | 1.02 |

- **The store at 1.5, on a 4K canvas with Look-ahead:** "frame ring: 4080x4448, 57 slots, 4138 MB", then
  53 slots and 3847 MB once the copy delay was fixed. The images were kept at the game's size, because a
  4K canvas shows them at 98% of it or more at any zoom (the source shrinks them below 83%).
- **Not measured:** the game's image at the default setting (only the runtime's recommended size), and
  the room for stabilization at these zooms.

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

## Source icons

How OBS shows the icons the sources give it (`shared\source_icon.h`). All of it holds for both plugins.

- **OBS 32.2.0 added custom source icons.** A source whose `icon_type` is `OBS_ICON_TYPE_CUSTOM`
  gives OBS one file for dark themes and one for light themes, through `get_dark_icon` and
  `get_light_icon`. OBS 32.0.2 and 32.1.2 map that type to their generic source icon, with a "TODO" to
  add custom icons (`OBSBasic::GetSourceIcon` in
  [OBSBasic_Icons.cpp](https://github.com/obsproject/obs-studio/blob/32.2.0/frontend/widgets/OBSBasic_Icons.cpp),
  read at each of those tags). On those versions the sources keep OBS's generic icon.
- **OBS frees the path with `bfree`** and loads the file with `QIcon`, picking it by whether the
  theme is dark (`OBSBasic::GetCustomIcon`). A null path, or one to a missing file, gives the generic
  icon. So the sources return `obs_module_file`'s string, which is null when the file is missing.
- **Checked in OBS 32.2.2's Sources dock**, dark theme, at 1:1: the headset beside OpenXR Mirror, the
  gauge beside Performance Overlay and the rings beside G-Meter Overlay, next to OBS's own icons. The
  light theme, the add-source dialog and the other places OBS shows source icons (the toolbar under
  the preview, Advanced Audio Properties) were not looked at on screen.

## The demo: recording and playing a lap

How Windows' own video encoder and decoder (Media Foundation) do with the eye images, and how the demo
source plays a recorded lap. Measured with the build of the pull request that added the demo, on one
graphics card.

- **The encoder takes the images straight from video memory.** A throwaway probe wrote D3D11 BGRA
  textures from Media Foundation's sample allocator to an HEVC `.mp4` with the sink writer (hardware
  transforms on, throttling off), and read them back with the source reader (advanced video processing
  to BGRA, on the GPU). On flat test images:

  | Size | Encode | Decode |
  |---|---|---|
  | 2508×2084 | 450 fps | 1000 fps |
  | 5084×4168 | 126 fps | 279 fps |

  H.264 also took 5084×4168 on this graphics card, at 123 fps. HEVC was kept because its format goes
  beyond 4096 pixels, where H.264's stops.
- **`WriteSample` refused every sample (`E_INVALIDARG`) until each buffer's current length was set**
  to its full size (`SetCurrentLength`). The allocator's samples come with a length of 0.
- **Decoded images are padded** to whole blocks: 2528×2112 for 2508×2084, 5088×4192 for 5084×4168. The
  demo copies the image's own size out of them.
- **The parts together, without OBS or the game,** in a throwaway harness that ran the real code: 180
  frames of a 2508×2084 test image (a gradient and a moving bar) recorded from a shared texture, as the
  ring's, then played for 5 s and copied by a second device, as a host's.
  - Recording took 0.1 ms per frame on the recorder's thread (draw into the encoder's texture, and
    write). The 2 s video took 5.0 MB.
  - Playing handed out 449 frames in 5 s (89.8 fps), and every copy succeeded.
  - The frames' moments stayed one refresh apart to 0.001 ms, across the loop's join too.
  - Every pose came back as recorded, within the angle measure's own float error (0.04°).
  - The pixels came back 2.2 levels of 255 off on average. Only the very first frame after the demo
    started was black. Each later loop's first frame was right.
  - The first three frames after the demo started were handed out 8 to 28 ms after their moment. Every
    later frame, and every loop's start, was within 8 ms.
- **The lap finder** gave the right decision in nine made-up sessions: no start when the lap counter
  moves at the pit exit, a start and an end at the two crossings, and a lap thrown away in the pits.
  Where LMU's lap counter moves when the car leaves the pits was not checked in the game.
- **Tried and dropped: recording at the game's own size, holding copies before the lap.** One real lap in
  LMU practice, OpenXR source, 5084×4168 at 90 Hz, the first build of the demo:
  - While a lap was asked for, the source kept its 52 to 55 stored images at the game's own size (4.4 GB),
    and the recorder copied every frame into the encoder's images to hold the last 12, from the press of
    the button on. Those 24 images took about 2 GB more.
  - The game dropped 4.7 frames per 10 s on the out-lap and 1.4 on the lap (repeated game frames in the
    10 s stats, while the game ran at 85 fps or more). Two earlier sessions on the OpenXR source without
    the recorder dropped 1.0 and 0.8 (median 0). The drops felt worse in the headset than the counts say.
  - The lap's video took 150 Mbit/s, about 19 MB per second of the lap. A video player struggled to play
    it. The encoder never ran out of images.
  - Its last 3 frames lay past the closing crossing (up to 25 ms), which reached the telemetry after them.
    The demo refused the lap for it.
  - Since then the lap is at most 2560 wide (`LapRecorder::kWidth`, a quarter of the pixels), nothing is
    copied before the first crossing (the ring keeps 24 frames, and the recorder takes the lap's frames
    from it once the crossing is known), and the last 12 frames stay held until the closing crossing is
    known. The harness scaled 5084×4168 test images into a 2560×2098 video at 0.2 ms per frame, 1.6
    levels of 255 off on average.
- **Not measured:** the encoder's cost beside a running game since the change, and the demo in OBS.

## Strength

The Strength setting scales each smoothing's own tuning: Look-ahead's time constants are multiplied by
it, Instant's cutoff and beta divided by it. Measured with `stage_replay --pipeline` at zoom 1.25, as the
mirror app's window, with the build of the pull request that added it.

- **In OBS, a level from 0 to 10.** Level 0 is strength 0, no smoothing. The table's strengths are the
  levels 1 (0.25), 3 (0.5), 5 (1) and 10 (2), and 1.5 is about level 8 (1.52).
- **Strength 0 before it was handled.** Instant's view stayed up to 0.135° off its image's direction (its
  dead zone still held the head), and Look-ahead's path solve divided 0 by 0, so the mirror app showed no
  readable view at all. With the fix, both views look where their images do, to 0.00002° (the trace's
  rounding), on the e2e scenario `no_smoothing`.
- **At 1 nothing changed:** `stage_replay --golden` wrote the same 26 MB as the build before, byte for
  byte, over all 52 runs.
- **Look-ahead,** lag p99 and the view's jitter still, on the racing motion (`racing+heartbeat`) and the
  edge test (`edge_test+heartbeat`, turns up to 315°/s):

  | Strength | Racing: lag p99 | Racing: jitter still | Racing: edge stops | Edge test: edge stops |
  |---|---|---|---|---|
  | 0.25 | 0.27° | 0.0020° | 0% | 0% |
  | 0.5 | 0.55° | 0.0007° | 0% | 0% |
  | 1 | 1.68° | 0.0012° | 0% | 0% |
  | 1.5 | 2.98° | 0.0012° | 0% | 0.6% |
  | 2 | 4.44° | 0.0014° | 0% | 4.0% |
  | 3 | 5.73° | 0.3250° | 2.8% | 12.7% |

  Above 2 the path's time constant outgrows the half second it sees ahead, so it runs into the edges and
  stops there, and the view shakes. The slider stops at 2.
- **Instant** changes less: on the racing motion lag p99 3.03° at 0.25, 4.40° at 1 and 4.87° at 2. Its
  catch-up near the edges, which the strength leaves alone, sets most of its lag.
- **Not checked:** in a recording, or on the demo's recorded lap.

## The demo in OBS

What the recorded lap's playback did in OBS, with the build of the pull request that added the demo.

- **Instant pinned to the raw image in the demo.** In a session playing the recorded lap, Instant's last
  resort stopped the view at an edge on 415, 144, 109 and 514 of the 900 ticks in four 10 s stretches. Each
  stop moved the view by the head's roll (4.1° in the stretch looked at), onto the image's own pose, so
  only the horizon changed with the setting. Instant alone on the lap's own poses and eye stopped on none of
  its frames. The difference was the timing: the demo handed each frame over 1.4 ms before its moment
  (median), the OpenXR layer's frames in the same session 17.2 ms before. The head pose at each tick's own
  moment then lay past the newest known pose and was extrapolated. The demo now hands each frame over 17 ms
  ahead. Not checked in OBS since.
- **The recorded lap's images and poses match.** Its images' frame-to-frame shift (a central patch, from
  the demo's own decoder) correlated with the head's recorded yaw turns at 0.994 with no offset between
  them, and fell off on both sides (0.979 one frame off, 0.785 ten frames off).
- **The demo's images 17 frames away from their poses in OBS.** On Look-ahead at Strength 2, the picture
  OBS drew looked like the raw headset view, the same as with Smoothing Off apart from the levelled
  horizon. Measured from 12 s of OBS's preview captured off the screen, each frame matched to its trace row
  by the stamp: a patch of the fixed cockpit moved on screen with the head's turns 16 to 17 ticks earlier
  (correlation 0.73 to 0.78), and with the computed view only at 0.46 to 0.53. The view the pipeline
  computed was 10 to 30 times calmer than the head, in that trace as in the game's. Every consecutive frame
  showed a new image, so no image was repeated. The recorded lap pairs each image with its own pose
  (correlation 0.994 to 0.997 at no offset), decoded back to back and at the demo's pace. That check covered
  only the lap's first frames, before the game's first dropped frame. The cause is the next finding.
- **Windows' decoder repeated images where the game had dropped frames.** Decoding the whole recorded lap
  gave 41 images more than the lap has frames, their times exactly one refresh apart. The `.mp4` itself
  held one sample per frame, at the frames' own uneven times. The converter to BGRA in the source reader
  (advanced video processing) evens the frame rate out: where a frame was missing, it repeated an image.
  The demo paired images with frames by counting, so from the first repeat, about 8 s into the lap, each
  image was drawn with an earlier frame's pose: 17 frames earlier halfway round, 41 at the end. A pose
  0.2 s early cancels none of the head's shake, which is why the view looked like the raw headset view.
  - Turning the converter's frame-rate conversion off (`MF_XVP_DISABLE_FRC` on the reader's video
    processor) gave one image per frame, each image's time within 0.1 ms of its frame's moment over the
    whole lap.
  - In the mirror app on the same lap, at Zoom 1.6 in a 1280×720 window, a fixed patch of the cockpit
    then moved 0.89 and 0.74 px per frame (sideways, up and down) with Smoothing Off, and 0.50 and 0.29 px
    with Look-ahead. Before the fix the two were about the same. Not checked in OBS.
  - The demo's earlier copy through 8 shared textures was not the cause of the 17 frames.
- **Tried and set aside: starting part way into the lap and pausing,** with a Position slider and a
  Pause button in the properties, and then OBS's media controls (next section). The demo plays the lap in a
  loop, from two buttons in the properties. Measured in a throwaway harness that drove the demo
  source as a host does on the recorded lap. Started at 50 %, it began at the right frame, paused it held
  one frame for 90 images in 1 s, and resumed with the next frame. Started at 99.5 %, it played across the
  line into the lap's start. All 525 images it handed out matched their frames decoded straight through
  from the video's start, to the grey level. The encoder had put a key frame every 90 frames.

## OBS's media controls

Tried for the demo and set aside: it plays in a loop, from buttons in the properties. How OBS 32's
frontend and libobs drive a source that sets `OBS_SOURCE_CONTROLLABLE_MEDIA`, read in OBS's own source:
its media controls (MediaControls.cpp), its Properties window (OBSBasicProperties.cpp) and libobs's
sources (obs-source.c).

- **Where they show:** OBS's media controls appear for any source with the flag. Their code checks only
  the flag, and the source's id only for which buttons show. The Properties window has no media controls,
  and its preview only draws the source: it passes no mouse or key input to it. Only the Interact window
  passes input to a source. Where the main window places the controls (under the preview) was not read.
- **The play button follows the state.** On selecting the source, the controls read
  `obs_source_media_get_state` once: stopped, ended or none show restart (↻), playing shows pause, paused
  shows play. Pressing it while stopped or ended calls the source's restart, while playing its pause,
  while paused its play.
- **After that the controls follow signals,** not the state. libobs signals `media_play`, `media_pause`,
  `media_restart` and `media_stopped` after each action. `media_started` and `media_ended` come only from
  the source (`obs_source_media_started`, `obs_source_media_ended`). A change the source makes by itself
  (a lap saved, the demo failing to start) needs `obs_source_media_ended`, or the button stays as it was.
- **Threads:** the actions are queued and run on the source's video tick, before its own `video_tick`,
  so a signal the source sends from its tick comes after libobs's own. The state, time and length are read
  straight from the source's callbacks on the UI thread.
- **The seek bar** pauses a playing source while it is dragged, calls `media_set_time` about 10 times a
  second while it moves and once on release, then plays again. With a length of 0 it shows 00:00:00.

## The empty state's facts

What the empty state's rows are made from before the game has run, measured on a Pimax Dream Air with
Pimax Play's Render Quality at 1.0.

- **Pimax tells it all without a game.** Its session reports the refresh rate and the eye. The size it
  renders the eye at is `getFovTextureSize` for the eye's field of view at the runtime's pixel density,
  the undocumented key `pixels_per_display_pixel_rate` (1.0 here): 5016 × 4168. The client mirror was
  2508 × 2084, half of it, in every lap and session measured.
- **OpenXR tells only the image size without a session.** `xrEnumerateViewConfigurationViews` gave the
  recommended 5084 × 4168, the size LMU renders at. The field of view and the refresh rate come only with
  a session. `XR_EPIC_view_configuration_fov` would give the field of view without one, and Pimax's
  runtime doesn't offer it.
- **The headset test** is that session. With the runtime loaded straight from its manifest (no loader,
  so no layer) it took about 1.4 s from loading the runtime to ending the session, with the text shown
  for 1 s. The views, located in the view space, gave the field of view LMU's frames carry (tangents
  1.307, 1.094, 0.984, 0.984) and no turn between the eyes. The frame timing gave 90 Hz.
  - Pimax refused every frame that held a quad layer (`XR_ERROR_RUNTIME_FAILURE`), so the text is drawn
    into the eyes' own images, centred where each eye looks straight ahead.
  - With the headset not worn, Pimax ran the session at about 13 frames a second, so the test waits for
    a time rather than for a count of frames.
  - An image written into a swapchain needs the context flushed before it is released: the compositor
    reads it on a device of its own.
- **Meta's runtime, with a Quest 3 on Link** (Meta Horizon Link, OpenXR 1.1.63). The test gave up about
  30 ms after it began, and the empty state showed no facts.
  - The runtime refused the loader handshake with `XR_ERROR_API_VERSION_UNSUPPORTED` (−4). The test
    offered API versions 1.0.0 to 1.0.34, the version of the headers in `third_party\openxr`. Offering
    every 1.x version fixed it, and the instance still asks for 1.0. Pimax's runtime accepts both ranges.
  - After that, the runtime reported "Meta Quest 3", a recommended 2720 × 2976 eye image, and the
    sRGB swapchain formats the test uses.
  - With the headset off the head, the session stayed idle (`XR_SESSION_STATE_IDLE`) for 30 s and never
    became ready, so the test still gives up after 5 s. With the headset on, it was ready at once and ran
    at 90 Hz with the views located.
- **The warm-up's jump.** The frame picker assumed a 40 ms copy delay until it fixed the one it
  measured. On the recorded lap that was 4 ms (copies at most 1.8 ms at the 95th percentile), so after
  2 s the output delay fell by 36 ms at once and the view jumped about 3 frames. The lap now starts at
  4 ms, and the game at the delay measured the last time it ran since OBS started.
