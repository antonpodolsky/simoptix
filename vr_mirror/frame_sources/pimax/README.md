# Pimax frame source

This frame source gives VR Mirror Pimax's client mirror, with the head poses and the pose Pimax
predicted for each display slot. It runs in VR Mirror, outside the game, and reads them through a PVR
session of its own. It needs Pimax Play.

## Terms

| Term | Meaning |
|---|---|
| frame source | The code that delivers game frames, head poses and timing to VR Mirror: the Pimax source (`vr_mirror\frame_sources\pimax\`) or the OpenXR source (`vr_mirror\frame_sources\openxr\`) |
| mirror app | `simoptix_vr_mirror.exe`, VR Mirror in a desktop window (`vr_mirror\mirror_app\`). The e2e tests run it |
| host | A program that runs VR Mirror's pipeline and shows its result: the OBS plugin or the mirror app |
| client mirror | A feature of the Pimax runtime. It gives other programs a half-size copy of the left eye image the game draws, and a signal for each new frame |
| PVR | Pimax's own name for its client interface: the client library `libPVRClient64.dll` that Pimax Play installs, and the table of functions it hands out. The project's code that wraps it is named after it |
| display slot | One of the regular moments, 90 per second, at which the headset shows a new image. The game draws each frame for one slot, with the head pose the runtime predicted for that slot |
| arrival slot | The display slot a game frame is assigned from when it arrived, once the timing noise is removed. While the game keeps up, it is the slot the frame was drawn for |
| drawn pose | The head pose a game frame was drawn with. Stage 2 of the pipeline answers it for each frame, and the code is named after it |
| game behind | The game's GPU has fallen behind and queues up to two frames. Each arrives one or two display slots after the slot it was drawn for |
| slot check | A way to find the drawn pose: it judges from thumbnails which display slot a frame was really drawn for |

## The source

- **It waits for the game.** After 2 s without a game frame (none yet, or the game quit), the source
  stops asking Pimax for head poses, the time and the display slots. It only waits for the game's
  frame signal, at most 50 ms at a time. It is back at full rate on the game's first frame, and catches
  that frame. The session stays open. Four times a second the source checks whether Pimax wants the
  session closed, which Pimax asks when another VR app takes over. The source then opens a new session.
- **Each frame's pose is the prediction for the slot it was drawn for.** A frame of the client mirror
  carries only its arrival.
  - The source's poll thread samples the head pose, the clock and the next display slot. For each new
    display slot it records the pose Pimax predicted.
  - Stage 1 gives each frame its arrival slot, from the moment the frame arrived. Stage 2 gives the
    frame that slot's prediction. While the game is behind, the slot check judges which slot the frame
    was really drawn for, and the frame gets that slot's prediction
    (`vr_mirror\core\pipeline\2_drawn_pose\README.md`).
- **It keeps each frame's image at the client mirror's size.** That is 2508×2084 in LMU at Render
  Quality 1.0, and it grows with Render Quality. The game overwrites the image about a frame later, so a
  host copies each frame as soon as it arrives. The image is sRGB-encoded twice, and the warp decodes
  one encoding.
- **Log lines** report the display's refresh rate, the client mirror switched on and off, and going
  idle after 2 s without a frame and back to full rate. They also report another VR app
  taking over and the new session, and a frame rate under 80 fps ("low mirror frame rate"). Every 10 s
  they report the calls into the Pimax runtime ("Pimax calls: ..."), and every 12 s the timing noise
  that stage 1 removed.
- **Its trace** (the source's Debug setting in OBS) is `trace_<time>_pimax.csv`, beside the frame
  trace. It has the head poses at 1 kHz, the pose Pimax predicted for each display slot, and every
  game frame's arrival.

## Files

| File | What |
|---|---|
| `pimax_source.*` | `PimaxSource`: the game's frames and the head pose for a host. Its poll thread ties the three parts below together and runs the pipeline's arrival timing (stage 1) |
| `pvr_session.*` | The PVR session: loading Pimax's client library and opening the session, re-creating it when another VR app takes over, the call lock and timed runtime calls |
| `pvr_mirror.*` | The client mirror: switching it on, the shared texture, the frame signal, and the wait while no game runs |
| `pvr_interface.h` | The part of Pimax's client interface that VR Mirror uses, declared by the project: the function table of interface version 1.26, the data it fills in, and the constants |
| `pvr_poses.h` | The recorded head poses and per-slot predictions under their own lock, and the hosts' lookups |
| `pimax.rsp` | Its `.cpp` files, as a `cl` response file that the hosts' build scripts list |
| `check\` | The layout check, a program of its own. Where Pimax's public PC SDK 1.26.1 is installed, `build.bat` builds and runs it. It compares `pvr_interface.h` with Pimax's headers, entry by entry and field by field |

The client mirror's runtime keys (`enable_client_mirror`, `client_mirror_handle`,
`client_mirror_sync_handle`) are undocumented Pimax internals, so a Pimax Play update could change
them. The mirror is switched off when a host stops, and also from the crash handler.
