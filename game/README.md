# Test game

`simoptix_game.exe` is a test game: an OpenXR program for the headset that shows a grid for judging
sharpness. It lets ways of drawing each eye be tried and compared in the headset without LMU. It draws
with Direct3D 11 into one swapchain per eye, in an sRGB format, as LMU does. Each eye can be drawn at
its own size.

It is a tool for contributors, built from source with `build.bat`. The API layer wakes up only in LMU,
so VR Mirror's OpenXR Mirror source shows nothing of the test game.

## Terms

| Term | Meaning |
|---|---|
| cycles per degree | How fine a pattern of stripes is: the number of dark and light pairs in one degree of the view. An image resolves a pattern up to half its pixels per degree |

## What it shows

The chart is a grid of 10° cells on a sphere around the head. It is fixed in the room and far away, so
both eyes see it alike, and only turning the head moves it. Its centre is straight ahead where the head
looked when the session began, on the horizon. Each cell holds:
- **top left:** a star of 32 dark and light spokes. Towards its centre the spokes get finer. Where they
  turn grey, the image can't resolve them, so the grey disc is larger in a blurrier image.
- **top right:** five rows of upright stripes, at 6, 9, 12, 15 and 18 cycles per degree. They show how
  sharp the image is across. Each row's number is on its left.
- **bottom right:** five columns of flat stripes, at the same frequencies. They show how sharp it is
  up and down. Each column's number is above it.
- **bottom left:** the cell's direction in degrees, right of the centre on the first line and above it
  on the second, and two checkerboards, at 6 and at 12 cycles per degree.

The patterns are dark and light around a mid-grey background, with a light line between the cells.
Every pattern is averaged over each pixel it falls in, as a well anti-aliased game draws. So how sharp
it looks depends only on the image's size, and on what the runtime and the headset do with the image.

The image's pixels per degree differ across the view and between headsets. The root README's "Headsets
and games" has each measured headset's eye image size.

## How to use it

1. Put the headset on, with its software running.
2. Run `game\bin\simoptix_game.exe` from a console. It finds an installed OpenXR loader (SteamVR's or
   Pimax's), and the loader starts the active OpenXR runtime.
3. Keep the console window in front: it takes the keys.

| Key | What it does |
|---|---|
| Space | Centres the grid straight ahead of the head, on the horizon |
| Q, Esc | Ends the session and quits |

The console shows each eye's size, the size the headset recommends, and when the session runs or
stops.

## Settings

The settings are given on the command line.

| Setting | What it does |
|---|---|
| `--left-scale <scale>` | Draws the left eye at `<scale>` times the width and the height the headset recommends, from 0.1 to 2. 1 by default |
| `--right-scale <scale>` | The same for the right eye |
| `--loader <path>` | The `openxr_loader.dll` to use, before SteamVR's and Pimax's |

The size of an eye is capped at the largest the runtime allows. For example, this draws the left eye at
the recommended size and the right one at half its width and half its height:

```
game\bin\simoptix_game.exe --right-scale 0.5
```

## Files

| File | What |
|---|---|
| `game_main.cpp` | The program: reads the settings, opens OpenXR, runs the frames and reads the keys |
| `game_settings.*` | The command line's settings |
| `game_openxr.*` | `GameOpenXr`: the OpenXR loader, the instance, the D3D11 session, the swapchains and the frame loop |
| `game_view.*` | Where the chart sits, and how one eye sees it, as the grid's shader takes it (plain logic) |
| `game_grid.*` | `GameGrid`: the shader that draws the chart into an eye's image |
| `build.bat` | Builds `bin\simoptix_game.exe` |

The Direct3D 11 declarations of OpenXR come from the API layer's
`vr_mirror\frame_sources\openxr\openxr_d3d11.h`.
