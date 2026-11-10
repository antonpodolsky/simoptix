# SimOptix

SimOptix is a set of sim racing tools for capturing stabilized VR footage and overlays.
The tools are available as OBS Studio plugins: VR Mirror adds sources with the stabilized headset
view, and Overlays adds a G-meter source. Both work with Le Mans Ultimate (LMU) and Pimax headsets.

## What you get

- **SimOptix: VR Mirror** adds two OBS sources with the stabilized view:
  - **SimOptix: Pimax Mirror** takes Pimax Play's copy of the left eye.
    - It is half the size the game draws.
  - **SimOptix: OpenXR Mirror** takes the game's own left eye.
    - It is the full size the game draws.
    - It needs an API layer in the game, which is experimental and works offline only (below).
- **Stabilization** is set by the source's Smoothing setting:
  - **Off:** the headset's view as it is, with no delay.
  - **Instant:** no delay, for live streams.
  - **Look-ahead:** half a second behind the game, for recording. It is the default.
  - OBS's audio, the G-meter and race auto-record follow the delay, so they stay in sync with the view.
- **Race auto-record** makes OBS record each LMU race by itself.
  - It records from the start lights to 5 s after the car stops past the finish.
- **SimOptix: Overlays** adds the **SimOptix: G-Meter Overlay** source.
  - It also works over a normal capture of a flat-screen game.
  - It plays a demo lap, so you can try it without the game.

## Status

SimOptix is early software. There are no ready-made downloads, so you build it from source.

- **OBS:** built against OBS Studio 32.2.2. Other versions haven't been tried.
- **OpenXR Mirror** and its API layer (`vr_mirror\frame_sources\openxr\`) are experimental. Unsigned,
  the layer works in LMU started without Easy Anti-Cheat. Signed with a code signing certificate
  Windows trusts, it loaded and worked in LMU started with Easy Anti-Cheat, and online play stayed
  enabled. It hasn't been tried in an online session.

## Headsets and games

| Game | VR Mirror | G-meter and race auto-record |
|---|---|---|
| LMU | Measured, with both sources | Read LMU's telemetry |
| Other VR games | Pimax Mirror should work, not tried. OpenXR Mirror's API layer loads only in LMU | The G-meter's demo lap only |

| Headset | Pimax Mirror | OpenXR Mirror |
|---|---|---|
| Pimax Dream Air | Measured | Measured |
| Other Pimax headsets | Works through Pimax Play, not measured | Should work, not tried |
| Other OpenXR headsets | Needs a Pimax headset | Should work, not tried |

To record at the source's native resolution, set OBS's canvas and output to the suggested size
(Settings → Video), and its frame rate to the headset's refresh rate. "Max zoom for native" is the
highest Zoom setting at which the recording stays native. Above it, the image is enlarged, and the
video gets softer.

| Headset | Source | Render Quality | Left eye image | Suggested canvas | Max zoom for native |
|---|---|---|---|---|---|
| Pimax Dream Air | Pimax Mirror | 1.0 | 2508×2084 | 1920×1080 | 1.31 |
| Pimax Dream Air | Pimax Mirror | 1.25 | 3134×2604 | 2560×1440 | 1.22 |
| Pimax Dream Air | OpenXR Mirror | 1.0 | 5084×4168 | 3840×2160 | 1.32 |

The image sizes were measured in LMU. `vr_mirror\obs_plugin\README.md` ("Recording at the source's
native resolution") has more canvases and zooms.

## SimOptix and Easy Anti-Cheat

LMU runs Easy Anti-Cheat, so SimOptix gets the game's frames and data only through official
interfaces.

- **Pimax Mirror, the G-meter and race auto-record** run in OBS, outside the game. Pimax Mirror asks
  Pimax Play for its copy of the game's left eye, a signal for each new frame, and the head poses. The
  G-meter and race auto-record read the telemetry that LMU publishes for outside tools. Both stay out
  of the game's process and files.
- **OpenXR Mirror** needs an API layer that OpenXR loads into LMU when the game starts. The layer
  copies the left eye image after each frame has gone to the headset, and only while an OpenXR Mirror
  source is shown in OBS. It passes every OpenXR call on unchanged, except that it asks the runtime for
  one time-conversion extension. Its README has the details (`vr_mirror\frame_sources\openxr\README.md`).

**Don't install the API layer on a PC used for online racing.** Racing online with the layer
installed hasn't been tried.
- **Unsigned,** the layer is refused by Easy Anti-Cheat. With it installed, LMU started the usual way
  showed Easy Anti-Cheat's "Untrusted system file" warning, and online play was disabled. The
  unsigned layer works in LMU started without Easy Anti-Cheat, which means offline.
- **Signed with a code signing certificate Windows trusts,** the layer loaded and worked in LMU started
  the usual way, with Easy Anti-Cheat, and online play stayed enabled. The layer is built unsigned
  unless you sign it with such a certificate of your own (Install, step 3).

`vr_mirror\frame_sources\openxr\uninstall.bat` removes the layer.

## Requirements

To run:
- 64-bit Windows (developed on Windows 11)
- OBS Studio 32 (built against 32.2.2)
- For Pimax Mirror: a Pimax headset with Pimax Play running
- For OpenXR Mirror: the API layer installed. Unsigned, it needs LMU started without Easy Anti-Cheat
- For live game data (the G-meter's values and race auto-record): LMU

To build:
- Visual Studio 2019 or later, or its Build Tools, with the C++ tools. Only 2019 has been tried.
- For reading LMU: LMU's telemetry headers. The build finds them in the game's folder, in any Steam
  library, and `LMU_SDK` can name their folder instead. Without them, the build says so, and both
  plugins build and read no game. The G-meter's demo lap still plays.
- For a signed API layer: a code signing certificate of your own that Windows trusts.

## Install

1. Close OBS.
2. Run `vr_mirror\obs_plugin\build.bat install` and `overlays\obs_plugin\build.bat install`. Each
   builds its plugin and copies it to OBS's plugin folder
   (`C:\ProgramData\obs-studio\plugins\<its name>`): the DLL to `bin\64bit`, and `manifest.json` with
   the plugin's name and version to `data`. Install both from one build, because they share the game's
   data and check that they match.
3. For OpenXR Mirror, also install the API layer. Read the warning above first, close LMU, and run
   `vr_mirror\frame_sources\openxr\install.bat`. It builds the layer and installs it with
   administrator rights, which Windows asks for. That layer is unsigned. For a signed layer, run
   `vr_mirror\frame_sources\openxr\build.bat`, sign
   `vr_mirror\frame_sources\openxr\bin\XR_APILAYER_SIMOPTIX_frame_source.dll` with your certificate,
   and run `vr_mirror\frame_sources\openxr\install.ps1` as administrator. `install.bat` would build
   it again, unsigned.
4. Start OBS and add a **SimOptix: Pimax Mirror**, **SimOptix: OpenXR Mirror** or **SimOptix: G-Meter
   Overlay** source. Each plugin's README describes its settings: `vr_mirror\obs_plugin\README.md`
   and `overlays\README.md`.

To remove a plugin, close OBS and delete the plugin's folder in `C:\ProgramData\obs-studio\plugins\`.

**SimOptix Setup** installs and removes both plugins and the layer in one program, and shows what is
installed. `setup\build.bat` builds both plugins and then setup, `setup\bin\simoptix_setup.exe`, one
file that carries all three. It takes the layer as it was last built, so build the layer first, and
sign it for a signed layer. While anything is installed, Windows' installed apps list SimOptix, and
its Uninstall removes everything. `setup\README.md` describes setup.

## Folders

| Folder | What |
|---|---|
| `vr_mirror\` | SimOptix: VR Mirror |
| `vr_mirror\obs_plugin\` | The OBS sources Pimax Mirror and OpenXR Mirror, and race auto-record (`race_auto_record\`) |
| `vr_mirror\frame_sources\` | Where the game's frames and head poses come from: `pimax\` through Pimax Play, and `openxr\` through the API layer and its reader. Each has a check program |
| `vr_mirror\core\` | The stabilization itself, the same in OBS and in the desktop window |
| `vr_mirror\mirror_app\` | VR Mirror in a desktop window, which the end-to-end tests run |
| `vr_mirror\e2e\` | End-to-end tests: the desktop window against a simulated Pimax headset and game, or a simulated game with the API layer |
| `vr_mirror\recording_analysis\` | Scripts that check a recording frame by frame, and turn a moment of it into a test |
| `overlays\` | SimOptix: Overlays: the G-meter (`gmeter\`, its image layers in `gmeter\design\`) and what any overlay can reuse (`common\`) |
| `shared\` | Code compiled into both plugins: logging, CPU counters, crash dumps, the files kept in `%LOCALAPPDATA%\simoptix` (the newest 10 of each kind), the view's delay, and the list of OBS functions they call (`obs_lib\`). `build\` finds Visual Studio and gives every build SimOptix's version |
| `shared\telemetry\` | One reader of the game's telemetry for all of OBS, and the G-meter's demo lap. `games\lmu\fake\` makes up LMU telemetry, to test without the game |
| `setup\` | SimOptix Setup: one program that installs and removes both plugins and the layer |
| `third_party\` | libobs 32.2.2 and the OpenXR headers, as published |

Each part's README describes what it does, its settings and its files:
- `vr_mirror\obs_plugin\README.md`
- `vr_mirror\core\README.md`, and a README for each stage of the pipeline in `vr_mirror\core\pipeline\`
- `vr_mirror\frame_sources\README.md`, `vr_mirror\frame_sources\pimax\README.md` and
  `vr_mirror\frame_sources\openxr\README.md`
- `vr_mirror\mirror_app\README.md`
- `vr_mirror\e2e\README.md`
- `vr_mirror\recording_analysis\README.md`
- `overlays\README.md` and `overlays\gmeter\design\README.md`
- `shared\telemetry\README.md`
- `setup\README.md`

## Contributing

`CONTRIBUTING.md` explains how to build, test and send a change. `.agents\skills\dev\` holds the rules
for the code and the same way of working in full, written for AI coding agents and people alike, with
what was measured and tried so far. `.agents\skills\docs\` holds the rules for writing the docs.
`AGENTS.md` points every coding agent at these skills.

## How SimOptix is built

SimOptix's development is AI-assisted, using software engineering best practices. It has a strict dev
workflow, code and product quality/correctness gates, and the author reviews the code. The work rests on
end-to-end tests with a simulated headset and game, and on decisions made from measurements.
`HOW_SIMOPTIX_IS_BUILT.md` describes these practices.

## Licence

```
SimOptix is a set of sim racing tools for capturing stabilized VR footage and overlays.
Copyright (C) 2026 Anton Podolsky

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, see <https://www.gnu.org/licenses/>.
```

Each source file carries the copyright line and `SPDX-License-Identifier: GPL-2.0-or-later`, which
points to this notice. The licence is GPL-2.0-or-later (`LICENSE`), and the OBS plugins build on OBS's
own GPL code. `THIRD_PARTY_NOTICES.md` lists the third-party code included.
