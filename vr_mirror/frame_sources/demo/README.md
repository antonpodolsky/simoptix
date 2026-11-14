# Demo frame source

This frame source plays the newest recorded lap in a loop, in place of the game, for a VR Mirror source's
demo (**Start demo**, `vr_mirror\obs_plugin\README.md`, "The demo"). The hosts take its frames as
they take the game's. The pipeline stabilizes them with the source's settings, so a change of Smoothing
or Zoom shows at once.

## Terms

| Term | Meaning |
|---|---|
| frame source | The code that delivers game frames, head poses and timing to VR Mirror: the Pimax source (`vr_mirror\frame_sources\pimax\`), the OpenXR source (`vr_mirror\frame_sources\openxr\`) or the demo source (`vr_mirror\frame_sources\demo\`) |
| recorded lap | One full lap of the game that a VR Mirror source's Record a lap saved: the raw left eye of each frame, and the head pose it was drawn with. The demo source plays it in place of the game |
| host | A program that runs VR Mirror's pipeline and shows its result: the OBS plugin or the mirror app |
| drawn pose | The head pose a game frame was drawn with. Stage 2 of the pipeline answers it for each frame, and the code is named after it |
| drawn slot | The display slot a frame source reports a frame was drawn for. The OpenXR source reports it. The Pimax source can't |
| display slot | One of the regular moments at which the headset shows a new image, one per refresh (90 per second at 90 Hz). The game draws each frame for one slot, with the head pose the runtime predicted for that slot |
| performance counter | The Windows high-resolution clock (`QueryPerformanceCounter`). All times in the code are seconds on it |

## The recorded lap

The OBS plugin's lap recorder (`vr_mirror\obs_plugin\lap_recorder\`) saves each recorded lap as two files of
one name in `%LOCALAPPDATA%\simoptix\recorded_laps`, `recorded_lap_<date>_<time>`:

| File | What |
|---|---|
| `.mp4` | The left eye of every frame of the lap, one video frame per game frame, in HEVC, at most 2560 pixels wide (a wider image scaled down). The bytes are the frame source's own encoding, so an image Pimax encodes twice is stored encoded twice |
| `.csv` | The eye's field of view and its turn in the head, the images' size, the headset's refresh period and the lap's length (line to line), then one row per frame: its moment in the lap and the head pose it was drawn with |

`recorded_lap.h` reads and writes the `.csv`. Its first lines say what each row holds.

## How it plays

- **A thread of its own** plays the lap on the performance counter. It hands each frame over 17 ms before
  the frame's moment in the lap, as a game hands a frame to the headset ahead of the display slot it was
  drawn for, so the head pose at an output's own moment is already known. It decodes the frame's image
  3 ms before that, and hands the frame over once the image is ready, with its drawn slot (the moment) and
  its drawn pose, as the OpenXR source does. The head path between frames is interpolated between their
  poses.
- **The images** are decoded on the graphics card by Windows' Media Foundation, on a device of its own.
  They go into 8 textures that the host's device opens through their shared handles. A host copies an
  image within a frame or two, and a texture is written again eight frames later.
- **The loop.** The lap ends on the line it started from. After its last frame, the next lap starts its
  first frame one lap length after the lap before, and the clock runs on. So frames stay one refresh
  apart across the join, and the smoothing carries on across it.
- **Falling behind.** A frame due more than 0.25 s ago (the PC stalled) moves the lap on to now, and
  play goes on from there.
- Decoding needs Windows' HEVC Video Extension. Without it, starting fails and the OBS log says why.

## Files

| File | What |
|---|---|
| `demo_source.h/.cpp` | `DemoSource`: the frame source, its thread and the hosts' side |
| `demo_video.h/.cpp` | `DemoVideo`: the lap's video, decoded into the shared textures |
| `recorded_lap.h/.cpp` | `RecordedLap`: the lap's `.csv`, and where the newest lap is |
| `demo.rsp` | The source files, for the OBS plugin's build |
