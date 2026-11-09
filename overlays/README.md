# SimOptix: Overlays (OBS plugin)

This plugin draws the game's telemetry over the video, as the **SimOptix: G-Meter Overlay** source.
It is built against OBS 32.2.2 as `simoptix_overlays.dll`. It works without Pimax, also over a capture
of a flat-screen game.

## Terms

| Term | Meaning |
|---|---|
| output delay | How far VR Mirror's view runs behind the game: about 1 s with Smoothest, about 0.5 s with Smooth, none with Instant, and 0 while the source is hidden. The G-meter, race auto-record and OBS's audio follow it |

The stabilized headset view comes from a separate plugin, VR Mirror (`vr_mirror\obs_plugin\README.md`).
While a VR Mirror source is shown, its view trails the game by the output delay. The G-meter stays in
step with the view. From the delay that VR Mirror publishes, the telemetry works out which moment of
the game the view is showing, and the G-meter shows what the car was doing at that moment
(`shared\telemetry\README.md`). It works this out from the delay alone, not by looking at the view.
Without VR Mirror the delay is 0.

## Folders

| Folder | What |
|---|---|
| `obs_plugin\` | The OBS module (`plugin.cpp` registers the sources) and `build.bat` |
| `gmeter\` | The G-meter: the source (`gmeter_source.cpp`), its Chrono look (`meter_chrono.*`), shapes (`meter_draw.*`) and labels (`meter_text.*`), the values it draws (`meter_view.h`), and its image layers, embedded by `gmeter.rc` from `design\` |
| `common\` | What any overlay can reuse: text (`text_raster.*`), images (`meter_image.*`), and the 1:1 properties preview (`properties_preview.*`) |

The demo lap that the G-meter can play without the game is in `shared\telemetry\demo_lap.*`, beside the
telemetry it stands in for.

## Install

1. With OBS closed, run `build.bat install` in `obs_plugin\`. The root `README.md` lists what the build
   needs. The script builds the plugin and copies it to
   `C:\ProgramData\obs-studio\plugins\simoptix_overlays\bin\64bit`. If you use the VR Mirror plugin,
   install it from the same build.
2. In OBS, add a **SimOptix: G-Meter Overlay** source.

Reading LMU needs its telemetry headers from the game's folder at build time, because their licence
doesn't allow copying them into this repo. The build finds the game in any Steam library. A build
without the headers says so and reads no game, and the demo lap still plays.

Crash dumps of either plugin go to `%LOCALAPPDATA%\simoptix`, at most two per OBS session. The newest
10 crash dumps are kept. When OBS starts, the plugins delete the oldest beyond 8, so that session's two
still fit. The OBS log says how many they deleted. The OBS log marks this plugin's lines
`[simoptix_overlays]`.

## G-Meter Overlay

The G-meter is a friction circle (G-G meter) drawn from LMU's telemetry, like the in-car G-meters of
Porsche's Sport Chrono or the Nissan GT-R. It has one look, **Chrono**:
- a black bezel with the brake and throttle bars in grooves
- a dark face with 1G, 2G and 3G rings under domed glass
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
- **Brake and throttle** fill the two grooves in the bezel: brake on the left in red, throttle on the
  right in green. While ABS or TC intervenes, its bar turns a darker red or green, and its label (ABS
  or TC, past the groove's upper end) lights up in the bright pedal colour.

The scale is fixed at 3 G, as the rings are in the image.

### How it is drawn

The bezel, face, rings, notches and glass are the design's image layers (`gmeter\design\`, embedded in
the plugin by `gmeter.rc`). The glass is a crescent-shaped reflection whose lower edge fades out. The
plugin scales each layer once to the source's size with a high-quality filter and draws it 1:1. Scaling
takes about 20 ms on OBS's graphics thread, once, when the source is created or its size changes. The
layers are 960 px, so the meter is sharpest up to that size.

The dot (with a glow), its tail, the bars and all the text are drawn live between the layers. The text
is the numbers, ABS and TC, and the 1G, 2G and 3G ring labels.
- The tail is a slightly warmer orange than the dot, so the dot stands apart from it.
- The live G and the highest values use the font's tabular digits, placed by their digit cells, so the
  digits don't shift as the values change.
- The live G is centred on its window to a fraction of a pixel, then 0.8 design px right (half a pixel
  at size 300), which looks centred.

**Text** is rasterised with DirectWrite (`text_raster.cpp`) at the size it is shown and drawn 1:1:
- grayscale anti-aliasing, because ClearType's colour fringes don't survive a video encode
- outlines grid-fitted vertically, so horizontal strokes land on whole pixels
- a slight contrast boost, so light strokes on the dark face don't read thin
- rotated text (ABS, TC) rasterised already turned
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

Everything else is fixed in `gmeter_source.cpp` (the constants at its top): the force the driver
feels, the 80 ms smoothing, the 1 s tail, and brake, throttle, ABS and TC always shown.

### Placing it in a scene

Add it to the scene above the VR Mirror source.
- A new G-meter starts in the bottom-right corner, sized in proportion to the canvas: 300 px on
  1920x1080 and 400 on 2560x1440. At 1080 its box sits 16 px from the right edge and 11 from the
  bottom. Move or resize it like any overlay.
- It always draws itself at the size it is shown on the canvas, so it stays sharp at any size. When the
  canvas changes, OBS keeps it in proportion. The OBS log says the size it draws at ("drawn at 400
  px").
- While you drag its size, it shows the last build stretched and rebuilds 0.3 s after the drag stops.
- Leave its scale filtering at **Disable** and leave it uncropped. OBS draws a cropped or filtered item
  at its base size first (300 px) and then stretches that.
- Its **Properties** window resizes itself when it opens, whatever size it was left at. The preview
  then shows the meter at exactly its size on the canvas (1:1), and again if that size changes while
  the window is open. The window moves up or left if it would leave the screen. The log says
  "properties preview ... (1:1)". OBS doesn't tell a source which window shows its properties. So the
  plugin takes the window OBS activates as it opens them, with the source's name in its title. This
  works in any OBS language.

LMU's shared memory (the official interface for outside tools) is read once for all meters and for
VR Mirror's race auto-record. It is read only while something needs it.

## Telemetry

The overlays read the game's telemetry from `shared\telemetry\` (see its README). One reader serves the
whole of OBS and is shared with VR Mirror. It runs only while something needs it: a G-meter on screen,
or race auto-record switched on.
