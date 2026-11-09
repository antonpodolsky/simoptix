# OpenXR frame source

This frame source gives VR Mirror the game's full-size left eye, with the exact head pose each frame
was drawn with. It has two halves: an OpenXR API layer in LMU, and the reader in VR Mirror that opens
what the layer shares.

**Experimental. Don't install it on a PC used for online racing.** Easy Anti-Cheat, which LMU runs
online, refuses the unsigned layer that this repo builds. With the layer installed, LMU started the
usual way showed an "Untrusted system file" warning, and online play was disabled. `uninstall.bat`
removes the layer.

## Terms

| Term | Meaning |
|---|---|
| frame source | The code that delivers game frames, head poses and timing to VR Mirror: the Pimax source (`vr_mirror\frame_sources\pimax\`) or the OpenXR source (`vr_mirror\frame_sources\openxr\`) |
| mirror app | `simoptix_vr_mirror.exe`, VR Mirror in a desktop window (`vr_mirror\mirror_app\`). The e2e tests run it |
| drawn pose | The head pose a game frame was drawn with. Stage 2 of the pipeline answers it for each frame, and the code is named after it |
| host | A program that runs VR Mirror's pipeline and shows its result: the OBS plugin or the mirror app |

## The two halves

- **The layer** is an OpenXR API layer that the OpenXR loader puts into LMU when the game starts. It
  gives VR Mirror the game's full-size frames, 5084×4168 per eye instead of the Pimax mirror's
  2508×2084, together with the exact pose each frame was drawn with and the moment it was drawn for.
- **The reader** (`OpenXrSource`) runs in VR Mirror, outside the game. It opens what the layer shares
  and hands the frames to the OBS plugin and the mirror app, like the Pimax source does.

## Where it stands

The layer is built for LMU started without Easy Anti-Cheat, which means offline and with online play
disabled. There, the version that only watched the game loaded and worked. It saw the game's two
5084×4168 image buffers and its 90 frames a second, and the game kept 90 fps.

In LMU started the usual way, Easy Anti-Cheat keeps the layer out:
- Unsigned, the DLL is refused as an "untrusted system file".
- Signed with a certificate Windows doesn't trust, the DLL still isn't loaded.
- Whether a trusted code-signing certificate gets it in is not known.

## What the layer does

- **It works only in LMU.** OpenXR loads every registered layer into every OpenXR program (the game,
  Pimax Play's home, SteamVR tools). The layer wakes up only in a program called `Le Mans
  Ultimate.exe` that tells OpenXR its name is `LeMansUltimate`. In any other program, the layer hands
  back the runtime's own functions as soon as the program has started OpenXR. After that none of its
  code runs, and it writes nothing.
- **It passes every call on unchanged, with one exception.** When LMU makes its OpenXR instance, the
  layer also asks for the runtime's time conversion (`XR_KHR_win32_convert_performance_counter_time`),
  if LMU didn't and the runtime offers it. VR Mirror needs it to put the moment each frame was drawn for
  on its own clock. Without it, the layer still loads but gives VR Mirror no frames.
- **It hands each frame to VR Mirror** (the SimOptix: OpenXR Mirror source in OBS,
  `vr_mirror\obs_plugin\README.md`). After the frame has gone on to the headset, the layer provides:
  - A record of the frame in a small piece of shared memory. It holds the moment the game drew the
    frame for, and the pose and field of view of each eye.
  - While VR Mirror is reading, one copy of the left eye image into one of three textures that VR
    Mirror can open. The copy runs on the game's graphics card after the frame's own work and takes
    about 0.2 ms of it (the layer measures it). The frame has already gone to the headset, so the
    headset gets it no later.

  The layer knows a copy is finished by a marker it checks at the next frame without waiting, and
  then tells VR Mirror. If VR Mirror is still reading the texture that is next in turn, the layer
  skips that frame's copy, so the game is not held up.
- **It copies only while VR Mirror reads.** The three textures take 85 MB each at 5084×4168, which is
  254 MB of the graphics card's memory. The layer makes them when VR Mirror starts reading and lets
  them go 10 s after it stopped. With nobody reading, the layer only writes each frame's record.
- **In LMU, it writes one log per game start,**
  `%LOCALAPPDATA%\simoptix\openxr_frame_source_<date>_<time>.log`. The newest 10 are kept. Each game
  start that reaches OpenXR deletes the oldest beyond that.
- **The log's first line says that Windows loaded the DLL into LMU,** before any OpenXR step. A game
  start with no new log means the DLL was never loaded, for example because anti-cheat blocked it. A log
  with this line alone means OpenXR never called the layer.
- **After that line, the log holds:**
  - what the game's OpenXR loader offered in the first handshake, and why the layer refused it if it did
  - that the layer loaded, which runtime and headset the game uses, and whether it asked for the time
    conversion
  - the swapchains the game makes: the image buffers it draws into, with their size and format
  - the layout of the frames it hands the headset: which buffer, which part of it, and the field of
    view. This is logged once, and again only if it changes.
  - when VR Mirror starts and stops reading, and when the textures are made and let go
  - every 10 seconds: the game's frame rate, how many frames were copied and why others weren't, and
    the copy's time on the graphics card (median, 99th percentile, longest)
- **Its footprint in the game is small.** Each call goes on to the runtime first, unchanged. The layer
  then reads what the game passed and the runtime returned.
  - Per frame, it does a few comparisons, writes the record, and copies while VR Mirror reads.
  - It uses no locks, no waits on the graphics card, no flushes and no allocations per frame.
  - It makes the textures on a thread of its own. It makes them on the game's thread only if the
    game's graphics device can't be used from another one.
  - A log line is copied into memory, and a thread of the layer's own writes it to the file. The
    game's threads never wait for the disk. That thread sleeps until there is a line. If it is busy,
    the line is skipped and the log says how many were.
  - Because the log thread runs until the game exits, the layer's DLL stays loaded in LMU until then.
- **It is one file.** The DLL has the C runtime built in and depends on nothing but Windows. It is the
  only file the install copies, besides its manifest.

## The reader

- **It waits for the layer.** With no game running, the reader looks for the layer's memory once a
  second. With the game running, it sleeps until the layer says a copy is finished, and it signs twice a
  second so the layer keeps copying.
- **Each frame's pose is the one it was drawn with,** exactly.
  - The pose is the head halfway between the two eyes the game drew (OpenXR's head), at the moment
    the game drew the frame for, put on the PC's performance counter with the layer's clock
    reading.
  - Between frames, the head's path is interpolated between their poses.
  - Each frame says the moment it was drawn for. Stage 1 shows the frame at the rhythm the frames
    arrive in, which is that moment when the frame is on time. The frame's drawn pose is that
    moment's pose (stage 2's reported methods).
- **It keeps each frame's image as large as the output needs.**
  - A host says how wide (`SetPictureNeed`: 1:1 in the middle of its output, within 4 GB for all its
    stored images).
  - The reader copies the image at that size, never above its own. It averages in linear light when
    the copy is smaller, and keeps the image sRGB-encoded once, as the game drew it.
  - While its graphics card reads one of the layer's textures, the reader holds the layer off it,
    and lets go as soon as the read is done.
- **Log lines** start with "OpenXR:". They report the layer found, the clock read, frames coming, and
  going idle after 2 s without a frame, and the game's session ending. Every 10 s they report the
  frames read and the images that were gone before they were copied.

## Files

| File | What |
|---|---|
| `openxr_layer_entry.cpp` | What the OpenXR loader calls: joins the chain of layers, decides whether this is LMU, and asks for the time conversion |
| `openxr_layer_calls.*` | The calls the layer looks at in LMU, what it logs from each, and what it hands on to the frame source |
| `openxr_layer_frames.*` | The game's swapchain images and the frame each end of frame shows, handed to the publisher |
| `openxr_publisher.*` | The frame records, the copies of the left eye image and the layer's own thread. It is plain D3D11 with no OpenXR calls. The e2e tests' fake game publishes with it too |
| `openxr_share.h` | The memory the layer shares with VR Mirror: its layout, and the rules for reading it |
| `openxr_d3d11.h` | The few Windows-specific OpenXR declarations that the layer and its check program use |
| `openxr_frame_layout.*` | The summary of each frame's layout and the log lines (plain logic, no OpenXR calls) |
| `openxr_layer_log.*` | The log file. `shared\local_files.*` names it and keeps the newest |
| `XR_APILAYER_SIMOPTIX_frame_source.json` | The manifest: how OpenXR finds the DLL |
| `frame_source.def` | The DLL's one exported function |
| `build.bat` | Builds the DLL and the check program into `bin\` |
| `install.bat`, `uninstall.bat` | Double-click to install or remove the layer. They ask for administrator rights and run the scripts below |
| `install.ps1`, `uninstall.ps1` | Install or remove the layer for every user (as administrator) |
| `openxr_source.*` | `OpenXrSource`: the reader, a frame source for the hosts (`vr_mirror\core\adapters\d3d\frame_source.h`) |
| `openxr_pictures.*` | `OpenXrPictures`: the layer's textures opened on a host's device, and the copy of an eye image into the host's store at the size it needs |
| `openxr.rsp` | The reader's `.cpp` files, as a `cl` response file that the hosts' build scripts list |
| `check\` | The check program. It runs the built layer the way the game will, before the layer goes anywhere near the game: with LMU's name, a D3D11 session, one image buffer per eye in the game's format, and five seconds of frames, through a real OpenXR loader. It reads what the layer publishes as VR Mirror would |

Installed, the DLL and its manifest are in `C:\Program Files\simoptix\openxr_frame_source\`. They are
registered in `HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit` (value 0 = on, 1 = off).
