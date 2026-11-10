# Frame sources

A frame source delivers the game's frames and head poses to VR Mirror. VR Mirror has two:
- `pimax\` reads them through Pimax's software.
- `openxr\` reads them from an OpenXR API layer that OpenXR loads into the game.

## Terms

| Term | Meaning |
|---|---|
| mirror app | `simoptix_vr_mirror.exe`, VR Mirror in a desktop window (`vr_mirror\mirror_app\`). The e2e tests run it |
| display slot | One of the regular moments at which the headset shows a new image, one per refresh (90 per second at 90 Hz). The game draws each frame for one slot, with the head pose the runtime predicted for that slot |
| PVR | Pimax's own name for its client interface: the client library `libPVRClient64.dll` that Pimax Play installs, and the table of functions it hands out. The project's code that wraps it is named after it |
| client mirror | A feature of the Pimax runtime. It gives other programs a half-size copy of the left eye image the game draws, and a signal for each new frame |

## The two sources

| Folder | What |
|---|---|
| `pimax\` | Pimax's client mirror: a half-size copy of the game's left eye, with a signal for every new frame. A PVR session of its own reads it, together with the head poses and the pose Pimax predicted for each display slot. The OBS plugin (SimOptix: Pimax Mirror) and the mirror app run it. Its README says what it does |
| `openxr\` | An API layer that loads only into LMU and copies the game's full-size left eye with the pose each frame was drawn with and the moment it was drawn for. The copy happens after the frame has gone to the headset and only while VR Mirror reads. Also the reader that hands those frames to the OBS plugin (SimOptix: OpenXR Mirror) and the mirror app. Unsigned, the layer works only in LMU started without Easy Anti-Cheat. Its README says what it does |

Only the API layer goes into the game. OpenXR's own mechanism for API layers loads it.
