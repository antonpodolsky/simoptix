# Frame sources

A frame source delivers the game's frames and head poses to VR Mirror. VR Mirror has two:
- `pimax\` reads them through Pimax's software.
- `openxr\` reads them from an OpenXR API layer that OpenXR loads into the game.

## Terms

| Term | Meaning |
|---|---|
| mirror app | `simoptix_vr_mirror.exe`, VR Mirror in a desktop window (`vr_mirror\mirror_app\`). The e2e tests run it |
| display slot | One of the regular moments, 90 per second, at which the headset shows a new image. The game draws each frame for one slot, with the head pose the runtime predicted for that slot |
| PVR | Pimax's own name for its client interface: the client library `libPVRClient64.dll` that Pimax Play installs, and the table of functions it hands out. The project's code that wraps it is named after it |
| client mirror | A feature of the Pimax runtime. It gives other programs a half-size copy of the left eye image the game draws, and a signal for each new frame |
| host | A program that runs VR Mirror's pipeline and shows its result: the OBS plugin or the mirror app |

## The two sources

| Folder | What |
|---|---|
| `pimax\` | Pimax's client mirror: a half-size copy of the left eye the game renders (2508×2084 for LMU at Render Quality 1.0), with a signal for every new frame. A PVR session of its own reads it, together with the head poses and the pose Pimax predicted for each display slot. The OBS plugin (SimOptix: Pimax Mirror) and the mirror app run it |
| `openxr\` | **Experimental.** An API layer that loads only into LMU and copies the game's full-size frames (5084×4168 per eye) with the pose each frame was drawn with and the moment it was drawn for. The copy happens after the frame has gone to the headset and only while VR Mirror reads. Also the reader that hands those frames to the OBS plugin (SimOptix: OpenXR Mirror) and the mirror app. Unsigned, the layer works only in LMU started without Easy Anti-Cheat. Its README says what it does and where it stands |

Only the API layer goes into the game. OpenXR's own mechanism for API layers loads it.

## pimax

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
