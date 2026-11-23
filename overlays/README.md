# SimOptix: Overlays (OBS plugin)

This plugin draws the game's telemetry and performance over the video, as two sources: **SimOptix:
G-Meter Overlay** and **SimOptix: Performance Overlay**. The plugin is built against OBS 32.2.2 as
`simoptix_overlays.dll`. The G-meter works without Pimax, also over a capture of a flat-screen game. The
performance overlay needs the OpenXR API layer in the game (`vr_mirror\frame_sources\openxr\README.md`).

## Terms

| Term | Meaning |
|---|---|
| output delay | How far VR Mirror's view runs behind the game: about 0.5 s with Look-ahead, none with Instant, and 0 while the source is hidden. The G-meter, race auto-record and OBS's audio follow it |
| headroom | The part of a frame's time budget left on the CPU or the GPU: 100 × (budget − time) / budget, in percent, where the budget is one frame's time at the target rate. Negative when a frame takes longer than its budget |

The stabilized headset view comes from a separate plugin, VR Mirror (`vr_mirror\obs_plugin\README.md`).
When both are installed, they share two things through memory that each opens on its own:
- the output delay of the VR Mirror source on screen (`shared\program_delay.h`)
- the game's telemetry, read once for both and handed out already matched to the moment the view
  shows (`shared\telemetry\README.md`)

While a VR Mirror source is shown, its view trails the game by the output delay. The G-meter stays in
step with the view. From the delay that VR Mirror publishes, the telemetry works out which moment of
the game the view is showing, and the G-meter shows what the car was doing at that moment. It works
this out from the delay alone, not by looking at the view. Without VR Mirror the delay is 0.

## Folders

| Folder | What |
|---|---|
| `obs_plugin\` | The OBS module (`plugin.cpp` registers the sources), the DLL's version resource (`version.rc`) and `build.bat` |
| `gmeter\` | The G-meter: the source (`gmeter_source.cpp`), its Chrono look (`meter_chrono.*`), the values it draws (`meter_view.h`), and its image layers, embedded by `gmeter.rc` from `design\` |
| `performance\` | The performance overlay: the source (`performance_source.cpp`), its Chrono and OLED looks (`performance_look.*`), what each cell shows (`performance_view.*`), and its image layers, embedded by `performance.rc` from `design\`. The frame times it reads are in `shared\performance\` |
| `common\` | What any overlay can reuse: shapes (`meter_draw.*`), labels (`meter_text.*`), image layers (`meter_image.*`), the box on the canvas that keeps an overlay drawn at its shown size and places a new one (`overlay_box.*`), and the 1:1 properties preview (`properties_preview.*`). Text is rasterised by `shared\text_raster.*`, which VR Mirror uses too |

The demo lap that the G-meter can play without the game is in `shared\telemetry\demo_lap.*`, beside the
telemetry it stands in for.

## Install

1. With OBS closed, run `build.bat install` in `obs_plugin\`. The root `README.md` lists what the build
   needs. The script builds the plugin and copies it to
   `C:\ProgramData\obs-studio\plugins\simoptix_overlays\bin\64bit`. It also copies the plugin's
   `manifest.json` to `data` beside `bin`. OBS 32 reads the plugin's name and version from it. If you use the VR Mirror plugin,
   install it from the same build.
2. In OBS, add a **SimOptix: G-Meter Overlay** or a **SimOptix: Performance Overlay** source.

Reading LMU needs its telemetry headers from the game's folder at build time, because their licence
doesn't allow copying them into this repo. The build finds the game in any Steam library. A build
without the headers says so and reads no game, and the demo lap still plays.

Crash dumps of either plugin go to `%LOCALAPPDATA%\simoptix`, at most two per OBS session. The newest
10 crash dumps are kept. When OBS starts, the plugins delete the oldest beyond 8, so that session's two
still fit. The OBS log says how many they deleted. The OBS log marks this plugin's lines
`[simoptix_overlays]`.

The plugin writes its version to the OBS log when OBS loads it
(`[simoptix_overlays] loaded, version 0.1.0`). Windows shows the same version in the DLL's
Properties, under Details. A build of a release shows
its number (`0.1.0`). Any other build adds the commits since that release and the commit
(`0.1.0+12.gabc1234`).

## G-Meter Overlay

The G-meter is a friction circle (G-G meter) drawn from LMU's telemetry, like the in-car G-meters of
Porsche's Sport Chrono or the Nissan GT-R. It has one look, **Chrono**:
- a black bezel
- a dark face with 1G, 2G and 3G rings under domed glass
- brake and throttle bars just inside the 3G ring
- a window under the centre that shows the live G

### What it shows

- **The dot** shows the force. Its direction from the centre is the direction of the force, and its
  distance is the amount, against the 1G, 2G and 3G rings.
- **The force the driver feels,** as in-car G-meters show it. Braking moves the dot forward (up), and a
  left turn moves it right.
- **The dot is lightly smoothed** (an 80 ms filter), because raw acceleration is noisy over kerbs.
- **The comet tail** is the dot's last second, shrinking and fading. It shows how braking blends into
  cornering.
- **The highest G** reached in each direction (braking, acceleration, left, right) stands outside the
  outer ring. It resets when a new driving session starts (the game's session or track changes, or the
  session restarts), and when the demo starts or stops.
- **Brake and throttle** fill two faint tracks just inside the 3G ring: brake on the left in red,
  throttle on the right in green. While ABS or TC intervenes, its bar turns a darker red or green.

The scale is fixed at 3 G, as the rings are in the image.

### How it is drawn

The bezel, face, rings, notches, pedal tracks and glass are the design's image layers (`gmeter\design\`, embedded in
the plugin by `gmeter.rc`). The glass is a crescent-shaped reflection whose lower edge fades out. The
plugin scales each layer once to the source's size with a high-quality filter and draws it 1:1. Scaling
takes about 20 ms on OBS's graphics thread, once, when the source is created or its size changes. The
layers are 960 px, so the meter is sharpest up to that size.

The dot (with a glow), its tail, the bars and all the text are drawn live between the layers. The text
is the numbers and the 1G, 2G and 3G ring labels.
- The tail is a slightly warmer orange than the dot, so the dot stands apart from it.
- The live G and the highest values use the font's tabular digits, placed by their digit cells, so the
  digits don't shift as the values change.
- The live G is centred on its window to a fraction of a pixel, then 0.8 design px right (half a pixel
  at size 300), which looks centred.

**Text** is rasterised with DirectWrite (`shared\text_raster.cpp`) at the size it is shown and drawn 1:1:
- grayscale anti-aliasing, because ClearType's colour fringes don't survive a video encode
- outlines grid-fitted vertically, so horizontal strokes land on whole pixels
- a slight contrast boost, so light strokes on the dark face don't read thin
- one weight heavier below 9 px

Rebuilding the four highest values and the live G at once takes 0.3–0.6 ms on the graphics thread.

### Properties

- **Meter type:** **Chrono**, the one look.
- **Start demo / Stop demo:** the meter plays a made-up 12 s lap instead of the game's telemetry. It
  lets you see the meter or set it up without driving. The lap has:
  - full braking from about 2.4 s, with ABS at about 3 s
  - a long right-hander
  - an exit with TC at about 6.5 s
  - a shorter brake into a left-hander

  The demo works with or without the game running. It isn't saved, so it is off again when OBS
  restarts. Starting or stopping it clears the tail and the highest values, so demo numbers never mix
  with real ones. The lap is in `shared\telemetry\demo_lap.cpp`.

Everything else is fixed in the code (`gmeter_source.cpp`, the constants at its top):
- The dot shows the force the driver feels.
- The dot is smoothed over 80 ms.
- The tail is 1 s long.
- Brake, throttle, ABS and TC are always shown.

### Placing it in a scene

Add it to the scene above the VR Mirror source.
- A new G-meter starts in the bottom-right corner, sized in proportion to the canvas: 300 px on
  1920x1080 and 400 on 2560x1440. At 1080 its box sits 16 px from the right edge and 11 from the
  bottom. Move or resize it like any overlay.
- It always draws itself at the size it is shown on the canvas, so it stays sharp at any size. When the
  canvas changes, OBS keeps it in proportion. The OBS log says the size it draws at ("drawn at 400 x
  400 px").
- While you drag its size, it shows the last build stretched and rebuilds 0.3 s after the drag stops.
- Leave its scale filtering at **Disable** and leave it uncropped. OBS draws a cropped or filtered item
  at its base size first (300 px) and then stretches that.
- Its **Properties** window resizes itself when it opens, whatever size it was left at. The preview
  then shows the meter at exactly its size on the canvas (1:1), and again if that size changes while
  the window is open. The window moves up or left if it would leave the screen. The log says
  "properties preview ... (1:1)". OBS doesn't tell a source which window shows its properties. So the
  plugin takes the window OBS activates as it opens them, with the source's name in its title. This
  works in any OBS language.

## Performance Overlay

The performance overlay shows the game's frame rate and how much of each frame's time is left on the
GPU and on the CPU. It has two looks, **Chrono** (a dark bezel with a sunken face under glass) and **OLED** (a black display with
segmented bars). Three cells sit side by side:

| Cell | Reading | Bar and colour |
|---|---|---|
| FPS | The game's frames in the last second | No bar. Red when the frame rate is more than 2 below the target |
| GPU HR | GPU headroom, % | Fills with the headroom. Green from 10 %, amber from 5 %, red below 5 % |
| CPU HR | CPU headroom, % | As GPU HR |

- **The target** is the headset's refresh rate, as the headset's runtime gives it to the game. A
  frame's budget is one refresh: 11.1 ms at 90 Hz. 34 % GPU headroom means the GPU finished each frame
  with 3.8 ms of that left.
- **The GPU time** of a frame runs from the game's start of the frame to its hand-over to the
  headset, on the graphics card. **The CPU time** runs from the moment the runtime lets the game start
  a frame to when the game asks to start the next.
- **A negative headroom** is shown as it is ("-17%"), with an empty bar. The frame took that much longer
  than its budget.
- **A red reading** turns its number red too, because green and red differ mostly in hue.
- **The readings change once a second.** Each is the average of the last whole second.
- **A reading that isn't known is left out:** its cell shows only its label. This happens without the
  game, without the API layer, in the first second after the source is shown, and for the CPU when the
  game starts its next frame before it has ended this one, which cuts its CPU time short. The GPU reading is
  unknown when the game begins its frames on one thread and ends them on another without protecting
  its graphics device for that.

### Where the numbers come from

The OpenXR API layer times the game's frames, and only while a Performance Overlay source is shown.
The source signs in a small piece of shared memory (`shared\performance\performance_share.h`), and the
layer stops timing 1.5 s after it stops signing. While it times, the layer reads the clock three times
per frame and puts two timestamps in the game's graphics queue (`vr_mirror\frame_sources\openxr\README.md`).
The plugin logs when it finds the layer's frame times ("performance: reading the game's frame times").

### How it is drawn

The bezel, face, dividers, empty bar tracks and Chrono's glass are the design's image layers
(`performance\design\`, embedded by `performance.rc`), scaled once to the source's size and drawn 1:1.
The labels, readings, % and bar fills are drawn live between them, in Bahnschrift. Text is rasterised
at the size shown as the G-meter's is, with the readings in tabular digits placed by their digit cells,
so they don't shift as the numbers change.

### Properties

- **Theme:** **Chrono** or **OLED**.

### Placing it in a scene

- A new performance overlay starts in the top-right corner, sized in proportion to the canvas: 208 ×
  109 px on 1920x1080. Its bezel's right edge sits 47 px from the canvas's right edge, in line with the
  G-meter's rim, and its top 42 px from the top. Move or resize it like any overlay.
- It draws itself at the size it is shown on the canvas, and OBS keeps it in proportion when the canvas
  changes, as for the G-meter. The OBS log says the size it draws at ("Performance Overlay: drawn at
  416 x 218 px").
- Its box has room for the shadow round the bezel, more below than above.
- Leave its scale filtering at **Disable** and leave it uncropped, as for the G-meter.

## Telemetry

The G-meter reads LMU's shared memory, the official interface for outside tools, through the telemetry
both plugins share (`shared\telemetry\README.md`). One reader serves the whole of OBS, and runs only
while something needs it: a G-meter on screen, or race auto-record switched on.

The plugin that runs the telemetry reader logs the reader's footprint every 10 s while it is connected
to a game, with that plugin's tag ("footprint: telemetry thread (Le Mans Ultimate) ...").
