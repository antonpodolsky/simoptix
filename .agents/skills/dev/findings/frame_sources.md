# Findings: frame sources

Terms: `reference\terms.md`. What the frame sources are: `vr_mirror\frame_sources\README.md`.

VR Mirror takes its images from the **Pimax game eye** (Pimax's client mirror,
`enable_client_mirror`) and, being tested, from the OpenXR layer (`findings\openxr.md`). Three other
images were built, measured and dropped, and their code removed. This file keeps why, and how each
worked, so one can be rebuilt from it.

| Image | What it was | Why it was dropped |
|---|---|---|
| Game window | LMU's own VR mirror window, captured with Windows Graphics Capture | It needs a DSR desktop at the window's size: 15–25% more GPU load, next to a game that needs all of it |
| SteamVR mirror | SteamVR's full-resolution left-eye mirror texture | It needs SteamVR as the OpenXR runtime, and SteamVR refreshes it at only 45 Hz on Pimax |
| Compositor mirror (the mirror app only) | Pimax's undistorted composited view, both eyes side by side | 2880×1440 for both eyes (about 1440×1200 per eye, against the game eye's 2508×2084). It includes Pimax's own overlays, and showed ghosting on moving objects |

The game eye is a half-size copy of what the game renders: 2508×2084 at Render Quality 1.0, 3134×2604
at 1.25. But it arrives with every game frame at 90 Hz and costs almost nothing. More detail comes
from Render Quality (`vr_mirror\obs_plugin\README.md`), from the OpenXR layer, or from Pimax adding a
full-size mirror ("What Pimax could add", below).

## Game window

LMU draws a desktop "VR mirror" window (`VrMirror=1` in its VR settings) from the full-resolution eye
image, so it holds about twice the detail of the game eye. Measured in LMU.

- **What it shows:** a fixed crop of the left eye, x 0.05–0.95 and y 0.25–0.75 of the eye image.
  - Found by capturing the game eye and the window at the same moment (replay paused, headset still),
    and matching the two images' edges over scale, position and a small aspect correction. Score: 0.95.
  - The crop becomes the warp's eye tangents: left = l − x0·(l + r), right = −l + x1·(l + r),
    up = u − y0·(u + d), down = −u + y1·(u + d).
  - LMU shows only half the eye's height. So a 16:9 view is narrower than from the game eye at the
    same zoom: about 60° wide against 76°.
- **Detail:**
  - With the window at 6880×2880 (DSR 4×), a 2560-wide output at zoom 1.5 drew from about 3765 source
    pixels across, against about 1656 from the game eye.
  - At 4587×1920, 2560×1440 was native at zoom 1.33.
- **Cost:** the DSR desktop and the capture took 15–25% of the GPU. A window image only comes with a
  desktop refresh, so the desktop must refresh well above 90 Hz. 165 Hz was used.
- **Capture:** Windows Graphics Capture on the game's largest visible top-level window, so nothing
  touches the game process.
  - A free-threaded frame pool of 6 buffers, `B8G8R8A8UIntNormalized`, cursor and border off.
  - Windows caps window capture at about 60 fps unless the session's `MinUpdateInterval` is set
    (1 ms), which needs Windows 11 24H2.
  - With an older Windows SDK, the build generated C++/WinRT headers from the installed Windows
    metadata (`cppwinrt.exe -input local`) and linked `windowsapp.lib`.
  - The captured image includes the title bar and borders. The inside is the client rectangle placed
    against `DWMWA_EXTENDED_FRAME_BOUNDS`.
- **Which game frame an image shows:** the window shows a game frame about 6–8 ms after Pimax receives
  it, plus 0–6 ms waiting for a desktop refresh. That is too close to the 11 ms between game frames
  for timing alone, so each window image was paired by content:
  - A 64×32 greyscale thumbnail of it (`vr_mirror\core\adapters\d3d\thumbnail.*`, still used by the
    drawn pose stage) was compared with thumbnails of the game-eye frames that arrived up to 40 ms
    before, cropped to the window's part of the eye.
  - The best match above a similarity of 0.6 won. Otherwise the arrival nearest the image time minus
    the running median delay won, if it was within half a frame.
  - An image that paired with the same game frame as the one before was a repeat, and was skipped.
- **Output timing:** window images arrive unevenly. So each was kept, and the host drew one output per
  game frame at a fixed delay after it: the window's 95th-percentile delay plus a present delay, 6–30
  ms. A missing image was replaced by the newest older one, re-projected to the current frame's head
  pose. Live test: 900 frames per 10 s, 0–4 re-projected, present jitter about 0.1 ms rms.
- **Colours:** the window is sRGB-encoded once, so the game eye's colour fix was skipped for it.

## SteamVR mirror

With SteamVR as the OpenXR runtime, LMU renders 5084×4168 per eye, and SteamVR's compositor offers a
mirror of it.

- **Connecting without touching the game:** load SteamVR's own `openvr_api.dll` from the runtime
  folder named in `%LOCALAPPDATA%\openvr\openvrpaths.vrpath` (`<runtime>\bin\win64\`). Call
  `VR_InitInternal2` as `VRApplication_Background`, which never starts SteamVR and isn't the scene
  app. `Init_NoServerForBackgroundApp` means SteamVR isn't running yet: try again every 2 s.
- **The image:** `IVRCompositor::GetMirrorTextureD3D11(Eye_Left, device)` gives a shader resource view
  of a shared texture (release it with `ReleaseMirrorTextureD3D11`).
  - Each time `GetFrameTiming` reported a new frame index, mip 0 of slice 0 was copied.
  - There is no image event, so the copy thread polled every millisecond.
  - The mirror was reopened every 5 s, and `VREvent_Quit` was acknowledged.
- **Its place in the game eye:** `IVRSystem::GetProjectionRaw(Eye_Left)` gives SteamVR's tangents (top
  is negative in OpenVR). Against the Pimax eye's tangents l, r, u, d, the image covers x
  (l − tL)/(l + r) to (l + tR)/(l + r), and y (u − tU)/(u + d) to (u + tD)/(u + d).
- **Pairing and output:** as for the game window, with no colour fix.

## Compositor mirror (the mirror app only)

The Pimax compositor's own output, before lens distortion, both eyes side by side.

- **Runtime keys** (undocumented, from strings in `pi_server.exe`): `enable_undistorted_mirror` = 1
  switches it on. There are two buffers, `undistorted_mirror_handle_0` and `_1`, each with a sync event
  (`undistorted_mirror_sync_handle_0`, `_1`), duplicated from `pi_server.exe` like the client mirror's.
- **Rate:** `record_fps` caps it, at 30 by default. It was set to 60 on start and put back on stop.
- **Layout:** 2880×1440, each eye fitted into its half with black padding. Each eye's area was measured
  from a CPU readback of the frame (lit pixels, columns and rows). It was trusted only within 8% of the
  eye's field-of-view shape, and measured again every 60 frames until then.

## Rebuilding one

1. Deliver each image as a texture, plus its area in the texture.
2. Pair it with the game frame it shows, and carry that frame's serial, arrival time and display slot.
   The head pose then comes from `PimaxSource` as the game eye's does, and the drawn pose stage
   still applies.
3. Give the image's part of the left eye as eye tangents (the crop formulas above), for the warp and
   for the smoothing's room.
4. Say whether the image carries the game eye's extra sRGB encoding.
5. Say how it signals new images (an event, or polling), so the copy thread can sleep otherwise.

## The PVR declarations

Since 2026-11-08 the Pimax frame source and the e2e fake runtime use the project's own declarations
of Pimax's client interface (`vr_mirror\frame_sources\pimax\pvr_interface.h`), written from scratch.
They replace the headers of Pimax's PC SDK 1.26.1, which say "All Rights reserved" and come with no
licence. Checked on 2026-11-08:

- **Layout:** the layout check (`vr_mirror\frame_sources\pimax\check\`) passes against the SDK 1.26.1
  headers. Five deliberate mistakes each failed it: a table entry moved, a field moved, a parameter's
  size, a return type, and the version number.
- **Builds:** no VR Mirror build opens a file from the SDK's folder (the compiler's list of included
  files).
- **Pimax's real client library:** Pimax Play installed (runtime 1.33.1) but closed. OBS loads the
  plugin, the library hands out the 1.26 table and initialises, and opening a session returns 6
  (Pimax's code for the runtime not answering). The build with the SDK's headers logged the same.
- **Not checked:** a session with Pimax Play running, in the headset.

## What Pimax could add

The client mirror is the best image there is for this: outside the game process, a signal for every
new frame, and almost no cost. Its limit is size: a 2:1 downscale of the game's eye image (2508×2084
of LMU's 5084×4168). These would make full-resolution footage possible in the Pimax runtime itself:

1. **A size option for the client mirror** (a scale, or a width and height). The server already has the
   full-size image. It costs one GPU copy per frame, about 85 MB at 5084×4168, only while a client has
   the mirror on.
2. **Per-frame data next to the mirror:** a frame index, the head (or eye) pose the frame was rendered
   with, and its predicted display time, so a tool could re-project each frame exactly.
3. **A "mirror-only" session type.** A tool session started before the game is asked to quit
   (`ShouldQuit`) when the game becomes the active client, and the server ends the process after 10 s.
   A flag for sessions that never submit frames would let tools stay connected.
4. **The colour encoding documented or fixed.** The mirror's bytes are sRGB-encoded twice: an `_SRGB`
   texture holding already-encoded values.
5. **A choice of eye**, and of the full eye or the undistorted crop.
