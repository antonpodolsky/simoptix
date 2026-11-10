# Contributing to SimOptix

Thank you for helping. This page is the short version of how to build, test and send a change.
`.agents\skills\dev\SKILL.md` has the full rules for the code and the same way of working, written for
AI coding agents and people alike. Beside it are longer procedures (`reference\`) and what was
measured and tried so far (`findings\`). They are plain Markdown and worth reading before a larger
change. `.agents\skills\docs\SKILL.md` has the rules for writing docs, and
`.agents\skills\code-style\SKILL.md` and `.agents\skills\code-practices\SKILL.md` the rules for writing code.
`.agents\skills\pull-requests\SKILL.md` and `.agents\skills\issues\SKILL.md` have the rules for writing
pull requests, commit messages, issues and comments. `.agents\skills\public-text\SKILL.md` says what
stays out of everything public.

`AGENTS.md` points coding agents at these seven skills. Codex, Cursor, Windsurf, Zed and Cline read it
on their own. Claude Code reads it through `CLAUDE.md`, and finds the skills through the short ones in
`.claude\skills\`. GitHub Copilot reads `AGENTS.md` or `.github\copilot-instructions.md`, which points
at the same skills. Aider reads it when started with `--read AGENTS.md`. Gemini CLI reads it when its
project settings set `context.fileName` to `AGENTS.md`.

## Terms

| Term | Meaning |
|---|---|
| PVR | Pimax's own name for its client interface: the client library `libPVRClient64.dll` that Pimax Play installs, and the table of functions it hands out. The project's code that wraps it is named after it |
| frame source | The code that delivers the game's frames and head poses to VR Mirror: the Pimax source or the OpenXR source (`vr_mirror\frame_sources\`) |
| host | A program that runs VR Mirror's pipeline and shows its result: the OBS plugin or the mirror app |
| mirror app | `simoptix_vr_mirror.exe`, VR Mirror in a desktop window (`vr_mirror\mirror_app\`). The e2e tests run it |
| fake runtime | The stand-in for Pimax's client library that the e2e tests use in place of a headset and a game |
| replay | A real session played back through the fake runtime: its head poses, the runtime's predictions, and every game frame at its real arrival time |
| stage replay | `stage_replay.exe`. It runs the pipeline's stages offline, without the mirror app, the fake runtime or the GPU |

## Before you start

- **Licence.** SimOptix is GPL-2.0-or-later (`LICENSE`), and what you contribute is licensed the same
  way ("Sign-off", below). Every new source file starts with a copyright line and the licence tag, as
  the existing ones do. Yours carries the year and your name: `Copyright (C) <year> <your name>` and
  `SPDX-License-Identifier: GPL-2.0-or-later`.
- **The game is accessed only through official interfaces.** LMU runs Easy Anti-Cheat. Don't inject code, hook, put DLLs in the game
  folder, or read the game's memory. Use only official interfaces:
  - Pimax's client interface (PVR)
  - LMU's shared memory
  - Windows capture
  - OpenXR's own mechanism for API layers, for the one layer in `vr_mirror\frame_sources\openxr\`

  Every build of that layer that goes into the game is signed with a code signing certificate Windows
  trusts, because Easy Anti-Cheat keeps an unsigned one out. Unsigned, the layer runs only in LMU
  started without Easy Anti-Cheat.
- **The game and Pimax's compositor come first.** Code that runs beside them keeps a minimal footprint:
  - no busy loops
  - no allocations per frame
  - no waiting on the GPU
  - no work while hidden or idle
  - never making the game, Pimax or OBS's render thread wait
- **Third-party code.** Open an issue before adding any. It must be compatible with GPL-2.0, and it is
  listed in `THIRD_PARTY_NOTICES.md` with its licence text. Never copy LMU's or Pimax's SDK headers into
  the repo. The builds find LMU's in the game's folder. The part of Pimax's interface that VR Mirror
  uses is declared in the project's own `vr_mirror\frame_sources\pimax\pvr_interface.h`, written from
  scratch.

## What you need

| For | Needs |
|---|---|
| Every build | 64-bit Windows. Visual Studio 2019 or later, or its Build Tools, with "Desktop development with C++". The builds find it with `shared\build\vs_env.bat`. Only 2019 has been tried |
| Reading LMU's telemetry | LMU installed through Steam, or `LMU_SDK` set to the game's `Support\SharedMemoryInterface` folder. The build finds that folder in any Steam library. Without it, both plugins build, the build says so, and they read no game |
| Trying the plugins | OBS Studio 32.2.2. For VR Mirror's Pimax source, a Pimax headset with Pimax Play running |
| The layout check of `pvr_interface.h` | Pimax's public PC SDK 1.26.1 (`pvr_windows_sdk_setup.exe` from developer.pimax.com) in `C:\Program Files\Pimax\Sdk`, or `PVR_SDK` naming its `Include` folder. Nothing else needs it |
| Signing the OpenXR layer | A code signing certificate of your own that Windows trusts, and `signtool`, which comes with the Visual Studio C++ workload. The repo builds the layer unsigned. Sign `bin\XR_APILAYER_SIMOPTIX_frame_source.dll` with SHA-256 and a timestamp, then install it with `install.ps1` |
| The e2e tests | A Windows SDK with the C++/WinRT headers (the Visual Studio C++ workload has one), Windows 10 1903 or later, and a D3D11 graphics card |
| The recording analysis | Python 3 with numpy. ffmpeg and ffprobe, on PATH or named by `FFMPEG` (the full path of `ffmpeg.exe`) |
| Formatting the code | Python 3, then `python -m pip install --user clang-format ruff`. pip installs both for your user only, and adds nothing to the repo. Versions 23.1.5 and 0.17.4 were tried |

## Build

Each part has a `build.bat`. Run it from a command prompt or PowerShell with its full path, for example
`cmd /c "<repo>\vr_mirror\obs_plugin\build.bat"`. Build output stays beside the script (`obj\`, `bin\`)
and is ignored by git. Builds must finish with no new warnings at `/W3`.

| Changed | Build | e2e tests at the end |
|---|---|---|
| `vr_mirror\core\`, `vr_mirror\frame_sources\pimax\` | Both plugins, `vr_mirror\mirror_app\build.bat`, `vr_mirror\e2e\build.bat` | Yes |
| `shared\` | The same, and `shared\telemetry\games\lmu\fake\build.bat` for a telemetry change, and `vr_mirror\frame_sources\openxr\build.bat` for `shared\local_files.*` | If the mirror app or the tests use the file. The local files test for `shared\local_files.*` and the crash dump |
| `vr_mirror\obs_plugin\`, `overlays\` | Both plugins | The local files test for the Debug setting's trace files. Otherwise no. Check in OBS (below) |
| `vr_mirror\mirror_app\` | `vr_mirror\mirror_app\build.bat` | Yes, for a change in behaviour |
| `vr_mirror\e2e\` | `vr_mirror\e2e\build.bat` | The scenarios that changed |
| `vr_mirror\frame_sources\pimax\pvr_interface.h` | As for `vr_mirror\frame_sources\pimax\`, then `vr_mirror\frame_sources\pimax\check\build.bat`, which checks the declarations against Pimax's headers. Without the SDK it says so and skips | Yes |
| `vr_mirror\frame_sources\openxr\` | Its `build.bat`, then its check program, then signed if the layer goes into the game | The local files test for the layer's log. Otherwise no |
| `vr_mirror\recording_analysis\` | Its `build.bat` (`patch_shift.exe`, `grid_shift.exe`) | No |
| `setup\` | Its `build.bat`, after the layer's `build.bat` (and signing). It builds both plugins too | No. Check its window and what it installs |

The two plugins are `vr_mirror\obs_plugin\build.bat` and `overlays\obs_plugin\build.bat`. With the
argument `install`, each also copies its DLL to `C:\ProgramData\obs-studio\plugins\<its name>\bin\64bit`,
and its `manifest.json`, which OBS 32 reads for the plugin's name and version, to `data` beside it.
OBS must be closed, because it holds the DLLs. Install both from the same build, because the telemetry
they share checks that they match.

## Test

SimOptix is tested by the end-to-end suite in `vr_mirror\e2e\` rather than by unit tests
(`vr_mirror\e2e\README.md` says what each scenario checks). It runs the real mirror
app against the fake runtime, which plays synthetic head motion or a replay. It captures the window the
way OBS does and measures what a viewer would see.

1. Build `vr_mirror\mirror_app\build.bat`, then `vr_mirror\e2e\build.bat`.
2. Close LMU and the mirror app. For runs whose numbers are compared, close Pimax Play too, because its
   compositor disturbs the timing.
3. Run `vr_mirror\e2e\bin\e2e_runner.exe`. To run some scenarios only, name them
   (`e2e_runner.exe head_still`). `--list` lists them. The 38 runs take about 9 minutes, each in a
   small window on top of everything. Don't use the PC meanwhile.
4. Compare the results with `.agents\skills\dev\reference\e2e_results.md`, and update the numbers there
   when they change for a reason. Each run's files are in `vr_mirror\e2e\out\<scenario>\`.

The local files test (`vr_mirror\e2e\bin\local_files_e2e.exe`) checks the files the plugins and the
OpenXR layer write under `%LOCALAPPDATA%\simoptix`: the Debug setting's traces, crash dumps and the
layer's logs. It runs the built plugins in a private copy of OBS, and the built layer in a fake game.
Build both plugins, the layer and `vr_mirror\e2e\build.bat` first, and close OBS and LMU. It takes about
a minute, and its first run copies OBS (466 MB).

The stage replay (`vr_mirror\e2e\bin\stage_replay.exe`) runs in about a second. Use it to find out why
a scenario fails, and to check that a refactor changes nothing (`.agents\skills\dev\reference\e2e.md`).

- New behaviour in the pipeline gets a scenario, or a new measure in an existing one.
- A pipeline bug gets a scenario that fails before the fix, when the fake runtime can produce the
  conditions. If it can't, extend the fake. Don't add test hooks to the mirror app.
- Never loosen a scenario's limits or remove a scenario to get a pass. Report failures with their
  numbers.

**Checking by hand.** The tests cover VR Mirror's core, the mirror app, and the files the plugins and
the layer write. Check the rest by hand: the OBS plugins, the G-meter, LMU's telemetry, the OpenXR layer
and real hardware timing.
- Install the plugins and read OBS's log (`%APPDATA%\obs-studio\logs`) for their lines. Crash dumps go
  to `%LOCALAPPDATA%\simoptix`, and the newest 10 are kept.
- Without the game, `shared\telemetry\games\lmu\fake\lmu_fake.exe` publishes a made-up lap on LMU's
  telemetry interface for 60 s, while LMU is closed. `--race` adds a race start and finish, for race
  auto-record.
- For visual changes, measure positions in a screenshot of the OBS preview. Judge sharpness only at
  1:1 (OBS's Preview Scaling → Canvas).
- For the OpenXR layer, build it and run its check program,
  `"vr_mirror\frame_sources\openxr\bin\check\Le Mans Ultimate.exe"`, with the headset on
  (`.agents\skills\dev\reference\openxr.md`).

Say in the pull request what you checked, with numbers, and what you couldn't check.

## The code in short

`.agents\skills\dev\SKILL.md` has the full rules ("Rules"). The most important:
- **Two independent plugins.** VR Mirror and Overlays each build, install and run alone. What both
  need is in `shared\` and compiled into each.
- **Layers, with dependencies pointing inwards.** `vr_mirror\core\` is independent of OBS, windows and
  where the frames come from. The frame sources and the hosts include it, and it includes none of them.
- **Logic apart from input and output.** Maths, filters, state machines and decisions take time and
  inputs as parameters and make no D3D, OBS, PVR, Win32 or shared-memory calls. The code that makes
  those calls stays thin.
- **Every pipeline stage can be swapped.** Each stage answers one question through a small interface.
  A new way of doing a stage is its own files, tried in the tests side by side before it becomes the
  default (`.agents\skills\dev\reference\pipeline.md`).
- **One responsibility per file.** Each header starts with a comment that says what the file is and
  what it deliberately doesn't know. Split files past about 500 lines.
- **Threads.** Comment which thread owns each member. Share only through `std::atomic` or a briefly
  held mutex. Threads catch everything, log it and stop cleanly.
- **Setting keys are saved in users' scenes.** Never rename them.
- **Names.**
  - A part's name is its folder's: `frame_sources\pimax\` holds `pimax_source.*` and `PimaxSource`.
  - File names and the names programs see are snake_case with `simoptix_`, and never have hyphens.
  - "PVR" names the code that wraps Pimax's client interface. "Pimax" names the rest: the frame source,
    the headset and Pimax's software.
  - What OBS shows is "SimOptix: <what>", matching the id.

  The details are in `.agents\skills\dev\SKILL.md`, "1b. Names".
- **Logging.** Log state changes once. Periodic stats go out at most every 10 s.
- **Style.** clang-format (`.clang-format`) lays out the C++ and HLSL, and ruff (`ruff.toml`) the
  Python. Run both on the files you changed before committing:
  `clang-format -i <files>` and `ruff format <files>`. The rules they can't apply (paragraphs, names,
  comments) are in `.agents\skills\code-style\SKILL.md`.

## Commits and pull requests

- Make one small commit per change. The subject is `Area: what changed` (for example `Overlays: the
  G-meter's live G moved up 1 px`), and the body says why.
- Docs move with the code. When behaviour, settings or files change, update the matching README in the
  same commit. READMEs describe what the project is and does. How to work on it, and the rules, go here
  and in `.agents\skills\dev\`. Write docs by `.agents\skills\docs\SKILL.md`, and run its check script
  before committing. Record approaches that were tried and dropped, with the measured reason, in the
  part's findings file (`.agents\skills\dev\findings\<part>.md`).
- Never commit build output, captures or local `.ini` files.
- Sign off every commit ("Sign-off", below).
- Send changes as a pull request against `main`. Say what changed, why, and what you verified: the
  builds, the e2e results if `vr_mirror\core\` or the mirror app changed, and what you checked by hand.
  The description covers that change only. How the work went and details of your own PC stay out.
  Plans, next steps and work on other branches go in an issue.
- Give the pull request one label, which decides whether it goes into `CHANGELOG.md`:
  - If users notice the change, label it `added`, `changed`, `fixed`, `removed` or `security`. Then
    write one to three lines in the Changelog section of its description. Write them for a VR sim
    racer, in plain words with no code names, as `.agents\skills\docs\SKILL.md` says ("The
    changelog").
  - If only contributors notice it (tests, refactors, contributors' docs, the skills, the build),
    label it `internal`, and leave the Changelog section empty.
- Keep personal information out of everything public: every file in the repo, commit messages, branch
  names, pull requests, issues and comments. That means recording names, dates and times (of your
  sessions and of your work on the code alike), tracks, cars and local paths. Git and GitHub keep every version.
  `.agents\skills\public-text\SKILL.md` lists what stays out. Before each commit,
  `python -I .agents\skills\public-text\check_public_text.py --staged` lists lines to review.

## Releases

SimOptix has one version for all its parts, in `VERSION` at the repo's root, such as 0.1.0. Every
build carries it, and a build that isn't a release adds the commits since the last one
(`0.1.0+12.gabc1234`). The maintainer makes the releases.

1. In a "Release: v<version>" pull request, bump `VERSION` and run
   `python -I .agents\skills\dev\release.py`. It writes the release's section of `CHANGELOG.md` from
   the labels and Changelog sections of the pull requests since the last release.
2. After it merges, tag the merge commit on `main` as `v<version>`, annotated.
3. Build the OpenXR layer from the tag, and sign it, because the version in it makes every build a new
   DLL.
4. Build SimOptix Setup, which builds both plugins and carries them and the signed layer. Sign it too.
5. Create the GitHub Release with that section of `CHANGELOG.md` as its notes, and the built files:
   SimOptix Setup, and zips of the plugins and of the layer.

While the version starts with 0, a patch release (0.1.1) holds only fixes. A minor one (0.2.0) adds or
changes things, and also anything that breaks. `.agents\skills\dev\reference\release.md` has the
details.

## Sign-off

Every commit ends with a sign-off line with your name and email:

```
Signed-off-by: Your Name <you@example.com>
```

`git commit -s` adds it from your git config (`user.name` and `user.email`).

By signing off, you certify the [Developer Certificate of Origin 1.1](https://developercertificate.org/)
(DCO). In short, you have the right to submit the change under the project's licence, and the record of
the change, with your sign-off, stays public.

Your contribution is licensed under the project's licence, GPL-2.0-or-later, and you keep its copyright.

If commits in your pull request lack the line, add it to every commit of your branch after `main`, then
push the branch again:

```
git rebase --signoff main
```

```
git push --force-with-lease
```
