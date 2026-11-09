---
name: dev
description: Dev workflow for SimOptix. The rules for the whole repo and the order to work in. Read it in full before any work on the repo.
---

# Dev workflow for SimOptix

SimOptix is a set of sim racing tools for capturing stabilized VR footage and overlays. Its
OBS plugins stabilize the headset view of Le Mans Ultimate, from a Pimax headset or through OpenXR,
and draw a G-meter over it. `README.md` has the layout of the repo, and each part has its own README
that describes it. This skill holds the rules for the whole repo (below) and the order to work in. Its
`reference\` files have the procedures, and its `findings\` files have what was measured and tried per
part. The terms are in `reference\terms.md`. Read it first if a word is unfamiliar.

The rules apply to the whole repo:
- `vr_mirror\`: VR Mirror with race auto-record, its frame sources, its core, the mirror app, e2e and
  the recording analysis
- `overlays\`: the G-meter
- `shared\`: the telemetry both plugins read, and the fake LMU

`CONTRIBUTING.md` at the repo's root is the same way of working for human contributors, in short. It
has the prerequisites, the build table (step 3), the e2e steps (step 5), the code rules, the sign-off
(step 6) and the pull-request rule (step 7). When one of those changes here, change it there in the
same commit.

The READMEs describe the project: what each part is and does, its settings, its files. How to work on
it is here, with the details in files beside this one. Paths below are relative to this skill's folder,
`.agents\skills\dev\`.

| File | What |
|---|---|
| `reference\terms.md` | The terms of this repo, general and coined |
| `reference\e2e.md` | Running the e2e suite and `stage_replay`, their options, adding a replay of a real session, tuning the fake |
| `reference\e2e_results.md` | The numbers to compare an e2e run with |
| `reference\recording_analysis.md` | Analysing a recording frame by frame, and reading older traces |
| `reference\pipeline.md` | Adding an implementation of a pipeline stage |
| `reference\mirror_app.md` | The mirror app's build and its use in the tests |
| `reference\openxr.md` | The OpenXR layer: build, check, sign, install, the Easy Anti-Cheat test |
| `findings\<part>.md` | What was measured, fixed, and tried and dropped, per part: `obs_plugin`, `frame_sources`, `openxr`, `overlays`, `telemetry`, `mirror_app`, `pipeline`, `e2e`, `recording_analysis` |

The rules for writing docs are in `.agents\skills\docs\SKILL.md`. Read that skill whenever you write or
change a README, `CONTRIBUTING.md` or a file of the skills.

The rules for writing code are in `.agents\skills\code-style\SKILL.md` (how it looks) and
`.agents\skills\code-practices\SKILL.md` (how it is built). Read both whenever you write, change or
review code (C++, HLSL, scripts, Python).

These four skills in `.agents\skills\` are the instructions for every coding agent. `AGENTS.md`,
`CLAUDE.md`, `.github\copilot-instructions.md` and the skills in `.agents\skills\` only point at them,
for the agents that read those files. Change the rules here, never in those files.

## Rules

### General

- **Never touch the game, with one exception.** LMU runs Easy Anti-Cheat. Don't inject code, hook,
  put DLLs in the game folder, or read game memory. Use only official interfaces: Pimax's PVR client
  interface, LMU's shared memory (`LMU_Data`), the OpenVR mirror, Windows capture, and OpenXR's own mechanism
  for API layers, for one layer only: `vr_mirror\frame_sources\openxr\`.
  - That layer is registered with the OpenXR loader and wakes up only in LMU. It stays out of the
    game folder.
  - It passes every call on unchanged, with one exception. When the game makes its OpenXR instance, it
    asks for the time conversion extension (`XR_KHR_win32_convert_performance_counter_time`) if the game
    didn't and the runtime offers it.
  - It reads only what the game hands the headset. It copies the left eye's image, and only while VR
    Mirror reads it. It never makes the game wait, and the footprint rules below apply to it twice
    over.
  - Nothing else goes into the game process. Every build of the layer that goes into the game is
    signed with a code signing certificate Windows trusts. Easy Anti-Cheat keeps an unsigned layer
    out, and online play is disabled while it is installed. Unsigned, the layer runs only in LMU
    started without Easy Anti-Cheat.
  - The repo holds no signing tools or certificate details. The user signs with a skill of their
    own outside the repo, whose description says it signs the OpenXR layer. Use it when there is
    one. Without it, say that the layer is unsigned (`reference\openxr.md`, "Signing").
  - The signed layer is tested offline before online (`reference\openxr.md`, "The Easy Anti-Cheat
    test").
- **Other games.** Before support for another game ships, check that game's terms for outside tools.
- **Licences.** The repo is GPL-2.0-or-later (`LICENSE`, `THIRD_PARTY_NOTICES.md`), and everything
  added must be compatible with it.
  - LMU's SDK headers stay in the game folder (`LMU_SDK`). Never copy them or Pimax's SDK headers into
    the repo. The part of Pimax's interface that VR Mirror uses is declared in the project's own
    `vr_mirror\frame_sources\pimax\pvr_interface.h`, written from scratch. Only its layout check
    (`vr_mirror\frame_sources\pimax\check\`) reads Pimax's installed SDK.
  - `third_party\` is vendored as-is. Don't edit it.
  - Ask before adding any new third-party code, and list it in `THIRD_PARTY_NOTICES.md` with its
    licence text beside it.
  - Every source file of the project's own starts with `Copyright (C) 2026 Anton Podolsky` and
    `SPDX-License-Identifier: GPL-2.0-or-later` in its own comment style. The full notice is in
    `README.md`, "Licence".
- **The game and the Pimax compositor come first.** Whatever runs alongside them keeps a minimal
  footprint:
  - no busy loops
  - no per-frame allocations in hot paths
  - no GPU flushes or CPU stalls on the GPU (event queries with `DONOTFLUSH`)
  - no work while hidden or idle
  - normal GPU priority (lowest priority caused black and frozen frames)
  - never make the game, Pimax or OBS's render thread wait: try-lock and skip instead
- **Fast loops, end-to-end last.**
  - After every change, build every affected target.
  - Run the e2e suite once at the end of a change to `vr_mirror\core\` or `vr_mirror\mirror_app\`, not
    after every edit.
  - Checks in the headset or in OBS come last. Tell the user exactly what to look at.
- **Git instead of backups.**
  - Make one small commit per change. The subject is `Area: what changed` and the body says why.
  - Sign off every commit with `git commit -s`, the maintainer's and every agent's (step 6).
  - An agent ends every commit message with the attribution line its own instructions give, if any.
  - Never commit build output, captures or local `.ini` files, or binaries the build can make (OBS's
    import library is made from `shared\obs_lib\obs.def`).
  - Once the repo has its GitHub remote, every change reaches `main` through a pull request (step 7).
- **Docs move with the code, each in its place.**
  - READMEs describe the project: what each part is and does, how it behaves, its settings as a user
    sees them, how to use it, and its files. They hold no build, test or working steps, no findings,
    and no history (the commit messages have that). When behaviour, settings or layout change, update
    the matching README in the same commit: the root folder table, `vr_mirror\obs_plugin\README.md`,
    `overlays\README.md`, `vr_mirror\e2e\README.md` and the part's own.
  - How to work on the repo, and these rules, go in this skill (`SKILL.md` and its `reference\` files)
    and, in short for human contributors, in `CONTRIBUTING.md`. Change them in the same commit as the
    way of working they describe.
  - Write all of them by the docs skill (`.agents\skills\docs\SKILL.md`), and run its check script
    before committing docs.
- **Record dead ends.** Approaches that were tried and dropped go into the part's findings file
  (`findings\<part>.md`) with the measured reason, so nobody retries them. Measurements and fixed bugs
  worth knowing go there too.
- **Measure, don't guess.** Smoothness, timing, alignment and placement claims need numbers: logs, e2e
  measures, or screenshots measured by script. Say what was not verified.

### Code structure

- **Two independent plugins.** VR Mirror (`vr_mirror\`) and Overlays (`overlays\`) build, install and
  run alone.
  - Neither includes the other's code, and neither loads or calls the other at run time.
  - What both need lives in `shared\` and is compiled into each: logging, CPU counters, the crash
    dump, the list of libobs functions, and the game's telemetry.
  - What they share while running passes through memory that each opens on its own: the output delay
    (`shared\program_delay.h`) and the game's telemetry, read by one reader for both
    (`shared\telemetry\`).
- **Layers, with dependencies pointing inwards:**
  - `vr_mirror\core\` holds the logic shared by every VR Mirror host. It is independent of OBS,
    windows, swap chains, UI and where the frames come from. Inside it, dependencies also point
    inwards:
    - `vr_mirror\core\common\` holds plain data and logic that every part shares. It includes only
      `common\`.
    - `vr_mirror\core\pipeline\` is the frame pipeline.
    - `vr_mirror\core\adapters\` is thin input and output glue (D3D, Windows). It may include the
      pipeline's interfaces, and the pipeline never includes it.
  - Frame sources (`vr_mirror\frame_sources\`) supply the game's frames and head poses. They are thin
    input and output glue like core's adapters. They may include core, and core never includes them.
    Each is included from the repo root (`#include "vr_mirror/frame_sources/pimax/pimax_source.h"`) and
    lists its own `.cpp` files (`pimax\pimax.rsp`). The OpenXR layer is a DLL of its own in the game
    and includes nothing of core.
  - Inside `vr_mirror\core\`:
    - Includes are root-relative, with the `/I..\core` that every build has: `#include
      "common/rot.h"` and `#include "pipeline/4_smoothing/smoothing.h"`. `shared\` is included from the
      repo root, which every build adds as `/I..\..`: `#include "shared/log.h"`.
    - A stage folder holds only its stage: `<stage>.h` (the interface and its stage-specific types),
      one `<implementation>.h/.cpp` pair per implementation, named for what it does, and a README. An
      implementation's private helpers are prefixed with its name
      (`slot_check_drawn_pose_lag.*`).
    - There is one name per concept in folder, file, class and config: `4_smoothing\`, `smoothing.h`,
      `Smoothing`, `PipelineConfig::smoothing`, `SmoothingKind`, and `lookahead_smoothing.h`,
      `LookaheadSmoothing`.
    - No stage includes another stage or an implementation. Stateless helpers beside the stages are
      fine (the smoothings call `picture_room\picture_room.h`). Only the glue (`pipeline.*`) and the
      builder (`pipeline_builder.*`) see several stages, and the builder alone sees the
      implementations.
    - `.cpp` basenames stay unique across core, the frame sources and each host, because every build
      writes its objects flat into one `obj\` folder (`/Fo:obj\`).
    - **The build lists.** The three builders (`vr_mirror\obs_plugin\build.bat`,
      `vr_mirror\mirror_app\build.bat`, `vr_mirror\e2e\build.bat`) each run in a folder beside core,
      so the response files' paths (`..\core\...` and `..\..\shared\log.cpp`) resolve for all of them.
      A new `.cpp` goes into the matching `vr_mirror\core\build\*.rsp`. `pipeline.rsp` holds only pure
      logic, so `stage_replay` can link it without the PVR or D3D libraries.
  - Hosts (`vr_mirror\obs_plugin\`, `vr_mirror\mirror_app\`) are thin glue. They provide the D3D
    device, the render target, settings storage and UI, and when to draw.
  - Telemetry (`shared\telemetry\`): game adapters (`shared\telemetry\games\`) feed one reader per OBS
    process. The reader feeds the overlays (`overlays\gmeter\`, with `overlays\obs_plugin\` as their
    OBS module) and race auto-record (`vr_mirror\obs_plugin\race_auto_record\`).
  - `vr_mirror\e2e\`, `vr_mirror\recording_analysis\`, the fake LMU and the OpenXR layer's check
    program are standalone programs.
  - Hosts include `vr_mirror\core\`, and core includes no host. Hosts include no other host, and no
    includes are cyclic.
- **Logic apart from I/O.**
  - Logic is maths, filters, state machines, layout, formatting and decisions. It makes no D3D, OBS,
    PVR, Win32 or shared-memory calls. Time and inputs come in as parameters (no
    `QueryPerformanceCounter` inside). That keeps the logic deterministic and reusable, and lets
    `vr_mirror\e2e\` (its `stage_replay` above all) drive it directly.
  - Adapters stay thin and make no decisions of their own. Examples: the PVR session, the LMU
    shared-memory reader, OBS callbacks, D3D drawing, GDI+ text.
  - Example: the G-meter's smoothing, highest values and session detection are logic. The reader thread
    and the OBS source are adapters.
- **One responsibility per file.**
  - A header's opening comment says what the file is and what it deliberately doesn't know (code-style
    skill, "Comments").
  - Split a file when it mixes responsibilities or grows past about 500 lines.
- **Small interfaces.**
  - Plain structs for data. Classes only where they hold state with rules.
  - Globals are only file-local statics in an anonymous namespace.
- **Threads.** Each thread-owned member says which thread owns it. Threads share only through
  `std::atomic` or a briefly held mutex, and no exception crosses a C API or a thread (code-practices
  skill, "Threads" and "Error handling across C APIs").
- **Settings.**
  - VR Mirror's settings live in `vr_mirror\core\common\settings.h` and are walked with
    `VisitSettings`.
  - Settings that only one host uses stay in that host.
  - Setting keys are saved in user scenes and config, so never rename them.
- **Logging.** Log state changes once, not every frame. Periodic stats go out at most every 10 s.
- **libobs pitfalls (OBS 32).**
  - `gs_color4v` is an empty stub. Use `gs_color` or effect parameters.
  - Immediate mode (`gs_render_start`) with vertex colours crashed OBS. Use a vertex buffer of the
    plugin's own, and unbind it after drawing.
  - Graphics work happens only on the graphics thread, or inside `obs_enter_graphics`.
  - Only functions listed in `shared\obs_lib\obs.def` can be linked. Add any new ones there.
- **Style.** C++17 on MSVC 2019 or newer, with no warnings at `/W3`. clang-format and ruff set the
  layout (step 3). How code looks and is built is in the code-style and code-practices skills.

### Tests

- **No unit tests.** The automated tests are the end-to-end suite in `vr_mirror\e2e\`. Don't add a
  unit-test layer.
- **End-to-end (`vr_mirror\e2e\`):** the real, unmodified `simoptix_vr_mirror.exe` runs against the
  fake PVR runtime (`vr_mirror\e2e\fake_pvr`), with its window captured like OBS. The suite covers the
  `vr_mirror\core\` pipeline: frames, smoothing, lag, horizon, black borders.
  - Run it once at the end of a change to `vr_mirror\core\` or `vr_mirror\mirror_app\`.
  - Its 54 runs take about 13 minutes (787 s on 2026-11-08). LMU and
    the mirror app must be closed, and nobody should use the PC meanwhile, so ask the user before
    running it.
  - Close Pimax Play too for runs whose numbers are compared, because its compositor disturbs the
    timing.
  - Compare the results with the numbers in `reference\e2e_results.md`, and update them there when
    they change for a reason.
- **What gets a scenario:**
  - New behaviour in the pipeline gets a scenario, or a new measure in an existing one.
  - A pipeline bug gets a scenario that reproduces it before the fix, when the fake runtime can produce
    the conditions. If it can't, extend the fake: fake the boundary, and add no test hooks to the
    mirror app.
- **What e2e doesn't cover:** the OBS plugins themselves (their sources, the G-meter, LMU telemetry)
  and real hardware timing. Check those by building, installing, reading the logs and measuring. Then
  hand over to the user with exactly what to look at.
- **Visual output** (overlays, labels, layout):
  - Check positions and gaps by measuring a screenshot with a script.
  - Allow for OBS's preview scaling: judge sharpness only at 1:1.
- **Copies kept in step.**
  - `vr_mirror\recording_analysis\replica.py` is a Python copy of the slot check's helpers
    (`vr_mirror\core\pipeline\2_drawn_pose\slot_check_drawn_pose_lag.cpp`). Change both together.
  - The G-meter's live parts in `overlays\gmeter\meter_chrono.cpp` and the tables in
    `overlays\gmeter\design\README.md` hold the same numbers. Change both together.
- **Thresholds.** Never loosen a scenario's limits or remove a scenario to get a pass without asking.
  Report failures with their numbers.
- **Before calling work done:**
  - Build every target that includes the changed files. `vr_mirror\core\` and
    `vr_mirror\frame_sources\pimax\` are used by `vr_mirror\obs_plugin`, `vr_mirror\mirror_app` and
    `vr_mirror\e2e`. `shared\` is used by those, by `overlays\obs_plugin`, by the fake LMU and (its
    logging) by the OpenXR layer.
  - The e2e suite passes, if `vr_mirror\core\` or `vr_mirror\mirror_app\` changed.
  - Say what wasn't run or checked.

## Correct, never easiest

- Always choose the solution that is correct: the one that does what the user asked, in the way the
  repo's rules and design call for. How much work, time or code it takes is never a reason to choose a
  different solution.
- Never recommend, offer or pick an option because it is the easiest, fastest, smallest or least work.
  Effort is not a criterion. If the correct solution is a lot of work, say so and do it.
- When options are compared, judge them only on correctness, the user's request, the rules above, and
  measured results. Don't list an easier but worse option as the recommended one.

## Talk in plain words

Everything the user reads (messages, plans, questions, recaps, READMEs) must be understandable at first
read, without decoding.

- Use short, everyday sentences, one idea each. Say what happens, in the user's terms: "the G-meter
  gets the car's readings from 0.5 s ago", not "consumers get the samples up to the shown moment".
- Avoid programmer shorthand: "samples", "users", "consumers", "handout", "adapter", "facade",
  "owner", "claim", "lazily", "ring", "block". Say what the thing is ("the car's readings", "a G-meter
  on screen", "the small memory area both plugins can see").
- Keep code names out of prose (functions, classes, files' internals) unless the user needs to open the
  file. Then give the path.
- If a technical word can't be avoided, explain it in the same sentence.
- Prefer a concrete example over an abstract rule.
- Before sending, reread it as the user. If a sentence needs the code to make sense, rewrite it.

## 0. Work in a git worktree

- Other sessions use the main folder, sometimes on another branch. Never switch its branch, and don't
  leave uncommitted changes there.
- Make a worktree from `main` under `.claude\worktrees\`
  (`git worktree add -b <name> .claude/worktrees/<name> main`) and work, build and test there. Its
  build output stays in it. Claude Code makes its own worktrees in that folder, and `.gitignore` leaves
  it out.
- When done, bring `main` up to it as step 7 says. Install the OBS plugins from the worktree with the
  finished code (step 4).

## 1. Before editing

- Read the README of the part being changed, its findings (`findings\<part>.md`: what was measured, and
  the dead ends not to retry), and the header comment of each file to touch.
- Decide where the change belongs.
  - Logic goes in its own module, with time and inputs passed in.
  - Adapters (OBS, D3D, PVR, shared memory, GDI+) stay thin.
- If the file is already over about 500 lines or mixes responsibilities, split out the part being
  changed first, as its own commit.

## 1a. Keep every stage swappable

Any stage of the pipeline (`vr_mirror\core\pipeline\`) can be replaced by a different implementation,
tried side by side, and measured. So can any other part with more than one way of doing its job. The
structure of the code keeps it that way. Check every change against these rules.

- **A stage answers one question, in its most general form.** The drawn pose stage says which pose a
  frame was drawn with. How it found out (a judged slot, a measured correction or a prediction) is its
  own business. Name the stage after its answer, not its method.
- **Nothing specific to one method crosses an interface.** A stage may hand on nothing that only one
  method has, such as a lag, a correction or a confidence. If it does, the interface is wrong.
  Make the answer more general instead of adding a field. Later stages must not be able to tell which
  method ran.
- **One or two calls per stage.** A stage takes its inputs and returns its answer. There are no call
  protocols between two stages, and no glue that interleaves them step by step. If two stages have to
  talk back and forth, one of them is really a service. Make it stateless (for example "how much room
  does this view have in this frame's image?") and hand it to the other.
- **Each implementation brings its own tuning,** as constants in its own files. The builder
  (`pipeline_builder.*`) makes each implementation without settings. Don't pass the shared settings
  struct (`MirrorSettings`) into stages, and don't add one method's parameters to it.
- **Stats go through one shared report.** A stage reports its numbers as name/value pairs, which hosts
  log without knowing the method. Don't add method-specific stats calls to an interface.
- **Adding a method touches only its own files,** plus one line in the builder and one value in
  `pipeline_config.h` (the steps, per stage, are in `reference\pipeline.md`). If more has to change,
  fix the interface first, as its own commit with identical output (`vr_mirror\e2e\stage_replay
  --golden`, byte for byte: `reference\e2e.md`, "Refactor guard"). Then add the method.
- **New methods are tried before they become defaults.** Select them by configuration in
  `stage_replay` and e2e (never test hooks), and measure them against the default. A default changes
  only on measurements that show it is better. Record the numbers, and record a method that lost as
  "Tried and dropped" in `findings\pipeline.md` (or the part's findings file).
- **When a change touches a stage that breaks these rules,** say so, and fix its interface first if the
  change depends on it.

## 1b. Names

One convention, everywhere. Check every new name against it, and rename what breaks it.

- **A part's name is its folder's name,** and its files, types and logs use it. The Pimax frame source
  is `frame_sources\pimax\`, `pimax_source.*`, `PimaxSource`. The OpenXR one is
  `frame_sources\openxr\`, `openxr_source.*`, `OpenXrSource`.
- **Files are named "which, then what",** in lower case with underscores (`pimax_source`, like the
  pipeline's `grid_arrival_timing`).
  - Some files belong to one of several parts of the same kind: the frame sources, the e2e fakes, a
    stage's implementations. Such a file starts with that part's name (`openxr_publisher`,
    `fake_pvr_replay.h`, `slot_check_drawn_pose_lag`). A file that wraps a vendor's interface starts
    with the interface's name instead (next rule).
  - A file that is one of a kind is named for what it is (`runner\analysis.cpp`, `common\rot.h`).
  - Types are the same words in PascalCase (`OpenXrPublisher`, `GridArrivalTiming`).
  - This also keeps `.cpp` names unique across builds.
- **"PVR" for what wraps Pimax's client interface, "Pimax" for the rest.**
  - Our code that wraps the interface's own things is named after it (`libPVRClient64.dll`):
    - its declarations (`pvr_interface.h`, `PvrInterface`) and their layout check (`pvr_layout_check`)
    - the session and its calls (`pvr_session.*`, `PvrSession`, `PvrCall`)
    - the client mirror (`pvr_mirror.*`, `PvrMirror`)
    - the head poses read through it (`pvr_poses.h`, `PvrPoses`)
    - the fake of its client library (`e2e\fake_pvr\`)
  - The Pimax frame source as a whole, the headset and Pimax's own software say Pimax (`PimaxSource`,
    Pimax Play, the Pimax runtime). The same applies to any vendor.
- **Names that programs and files see are `simoptix_` and snake_case, with no hyphens.** This covers
  DLLs (`simoptix_vr_mirror.dll`), folders, the exe, log tags (`[simoptix_vr_mirror]`), shared memory
  and event names. Environment variables are the same in capitals (`SIMOPTIX_VR_MIRROR_TRACE`).
  Everything a plugin or the layer writes goes under `%LOCALAPPDATA%\simoptix\` (traces in its
  `traces\`).
- **What OBS shows is "SimOptix: <what>", matching the id.** `simoptix_pimax_mirror` is "SimOptix:
  Pimax Mirror", `simoptix_openxr_mirror` is "SimOptix: OpenXR Mirror", and `simoptix_gmeter_overlay`
  is "SimOptix: G-Meter Overlay".
- **Words:**
  - "frame" is a game frame and its record. "Image" is the image in it. The code's remaining
    `picture_*` names (`picture_room`, `FakeGamePicture`) stay as they are.
  - "Drawn pose" is the head pose a frame was drawn with: the stage (`2_drawn_pose\`, `DrawnPose`),
    the setting `drawn_pose`, and the trace's `drawn_pose` column. It was called the picture pose,
    and the setting `picture_matching`, until 2026-11-08.
  - "Mirror" in a name ("VR Mirror", "Pimax Mirror", `MirrorView`, `vr_mirror\`) is our plugin and its
    sources. "The mirror" or "client mirror" in a sentence means only Pimax's client mirror
    (`PvrMirror`). A sentence that means the plugin says "VR Mirror".
  - "Source time" is the frame source's clock that the pipeline runs on.
- **Exceptions, fixed from outside:** the OpenXR layer's name (`XR_APILAYER_SIMOPTIX_frame_source`:
  OpenXR asks for the `XR_APILAYER_<VENDOR>_` form), OBS's own names (`obs-studio`, `obs64.exe`), and
  the skills' folders and names (`code-style`: the Agent Skills format asks for lower case and hyphens).

## 2. Edit

- Edit code with your agent's file-editing tools, not with shell commands.
- **Don't write files with PowerShell `Set-Content`, `Out-File` or `Get-Content -Raw`.** Windows
  PowerShell 5.1 adds a byte-order mark and garbles non-ASCII text (an en dash comes out as `â€“`).
- **Don't put code containing backslashes in bash heredocs or `python -c`.** The escapes get mangled.
  Write a script file with a file-editing tool and run that instead.
- Files in the working copy use LF line endings. Keep them that way.

## 3. Build (every change)

The automated tests are the e2e suite (step 5).

**Needed to build** (also in `CONTRIBUTING.md`, "What you need"):
- Visual Studio 2019 or later, or its Build Tools, with the C++ tools. Every `build.bat` finds the
  newest through `shared\build\vs_env.bat`, and only 2019 has been tried.
- LMU's headers: `shared\telemetry\sources.bat` finds them (`Support\SharedMemoryInterface` in the
  game's folder, in any Steam library, and `LMU_SDK` overrides). Without them, both plugins build, say
  so, and read no game (the G-meter's demo lap still plays).
- The formatters, clang-format and ruff, from PyPI for the current user. Nothing goes into the repo.
  Versions 23.1.5 and 0.17.4 were tried.

  ```
  python -m pip install --user clang-format ruff
  ```

  pip puts `clang-format.exe` and `ruff.exe` in the user's Python `Scripts` folder, and warns when
  that folder isn't on `PATH`.

The plugin builds make OBS's import library from `shared\obs_lib\obs.def`.

| Changed | Build | e2e at the end |
|---|---|---|
| `vr_mirror\core\`, `vr_mirror\frame_sources\pimax\` | Both plugins (step 4), `vr_mirror\mirror_app\build.bat`, `vr_mirror\e2e\build.bat` | Yes |
| `shared\` | The same, and `shared\telemetry\games\lmu\fake\build.bat` for a telemetry change | If the mirror app or e2e use the file |
| `vr_mirror\obs_plugin\`, `overlays\` | Both plugins (step 4) | No. Verify as in step 5 |
| `vr_mirror\mirror_app\` | `vr_mirror\mirror_app\build.bat` (if `simoptix_vr_mirror.exe` is running, build `simoptix_vr_mirror_dev.exe` instead) | Yes, for behaviour changes |
| `vr_mirror\e2e\` | `vr_mirror\e2e\build.bat` | The scenarios that changed |
| `vr_mirror\frame_sources\pimax\pvr_interface.h` | As for `vr_mirror\frame_sources\pimax\`, then `vr_mirror\frame_sources\pimax\check\build.bat`: the layout check, on a PC with Pimax's public PC SDK 1.26.1 (`pvr_windows_sdk_setup.exe` from developer.pimax.com) in `C:\Program Files\Pimax\Sdk`. `PVR_SDK` names another `Include` folder. Without the SDK it says so and skips | Yes |
| `vr_mirror\frame_sources\openxr\` | `vr_mirror\frame_sources\openxr\build.bat`, then its check program, then signed if it goes into the game (`reference\openxr.md`) | No |
| `vr_mirror\recording_analysis\` | `build.bat` there, for `patch_shift.exe` and `grid_shift.exe` | No |

- Run the build scripts through `cmd /c "<full path>\build.bat"` from PowerShell. A bare `build.bat`
  isn't found from another folder.
- A build must finish with no new warnings.
- Before committing code, run the formatters on the files the change touched, as the code-style skill
  says ("The formatters").

## 4. Install the OBS plugins

`build.bat install` in `vr_mirror\obs_plugin\` and in `overlays\obs_plugin\` builds each plugin and
copies its DLL to `C:\ProgramData\obs-studio\plugins\<its name>\bin\64bit`. OBS must be closed, because
it holds the DLLs. Install both from one build, because the telemetry both read checks that they match.

1. Build both first. If a build fails, leave OBS alone.
2. If OBS runs, close it the way its close button does, never by force: in PowerShell,
   `(Get-Process obs64).CloseMainWindow()`, then wait up to 20 s for the process to exit. If it doesn't
   exit, a recording or a dialog may be open. Tell the user and stop. Don't ask the user to close OBS.
3. Run both `build.bat install`.
4. If OBS was running, start it again in its own folder: `C:\Program Files\obs-studio\bin\64bit\obs64.exe`,
   with that folder as the working directory. Then read its new log for the plugins' lines with an
   error, a failure, a crash or an exception.

Crash dumps go to `%LOCALAPPDATA%\simoptix`. OBS logs are in `%APPDATA%\obs-studio\logs`.

## 4a. Take a design into the plugin

`overlays\gmeter\design\` holds what the G-meter's look is built from. Its README has the layers and
the size, place and colour of every live part.

- **The static parts are images.** `overlays\gmeter\design\meter.html` draws the meter's static parts
  and its glass reflection. Open it with `#base`, `#glass`, or `#preview` for both over a backdrop. To
  change them, change `meter.html` (the live parts stay out of it). Then render each layer at 2× with
  Edge, transparent outside the meter:

  ```
  msedge --headless=new --disable-gpu --hide-scrollbars --user-data-dir=<scratch folder>
    --default-background-color=00000000 --force-device-scale-factor=2 --window-size=480,480
    --screenshot=<overlays\gmeter\design\gmeter_<layer>.png>
    "file:///<repo>/overlays/gmeter/design/meter.html#<layer>"
  ```

  Edge is at `C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe`. Check each layer by
  script:
  - alpha 0 outside the meter
  - the strongest alpha as intended
  - a layer that wasn't meant to change still identical to the committed file, pixel for pixel

  Then look at `#preview`.
- **The plugin embeds the images** by name: `overlays\gmeter\gmeter.rc` (`GMETER_BASE` is
  `gmeter_base.png` and `GMETER_GLASS` is `gmeter_glass.png`, as RCDATA), read by
  `overlays\common\meter_image.cpp`. `overlays\obs_plugin\build.bat` compiles the `.rc`. So after
  re-rendering a layer, rebuild and install (steps 3 and 4). A new layer (another reflection, say)
  needs its own RCDATA line and its own `MeterImage` in `meter_chrono.h`. If the user picks it, it
  needs a setting too (`overlays\README.md` as well).
- **The live parts are code.** `meter_chrono.cpp` draws them from constants in design pixels on the
  480 px board, centre (240, 240), with angles clockwise from up (as in `meter_draw.h`). Keep those
  constants and the tables in `overlays\gmeter\design\README.md` the same, and change both together.
- **Text** is drawn live and never put in an image, because it would be shrunk with the image and go
  soft. DirectWrite (`text_raster.cpp`) rasterises it at the size shown. `meter_text.cpp` makes labels
  of it, placed by their ink to a fraction of a pixel, with numbers in the font's tabular figures.
  Measure a label change at 1:1 (sharpness, position) before looking in OBS.
- **Verify** as for any visual change (step 5). Screenshot the OBS preview at 1:1 and measure it
  against `meter.html#preview` rendered the same way (positions, gaps, glare strength). Centring and
  spacing are then corrected to look right, not to measure right (step 5, "Centring and spacing are
  optical").
- Go design folder first, then the plugin. Record designs tried and dropped, and why a spec is as it
  is, in `findings\overlays.md` ("The G-meter's design").

## 5. Verify

- **Behaviour in `vr_mirror\core\` or `vr_mirror\mirror_app\`:** run `vr_mirror\e2e\bin\e2e_runner.exe`
  once, at the end.
  - LMU and the mirror app must be closed, and the PC must not be used meanwhile. The 54 runs take
    about 13 minutes. Close Pimax Play too for runs that are compared, because its compositor disturbs
    the timing (`reference\e2e_results.md`).
  - How to run it, its options and `stage_replay` are in `reference\e2e.md`.
  - Compare the numbers with the ones in `reference\e2e_results.md`, and update them there when they
    change for a reason.
  - Add or extend a scenario for new pipeline behaviour, or a pipeline bug the fake can reproduce.
- **Visual changes (overlays, layout):**
  - Take a screenshot of the OBS preview with .NET `Graphics.CopyFromScreen`.
  - Crop it and measure positions and gaps with a script, to know where things really are.
  - The preview is scaled down, so judge sharpness only at 1:1. The user can set **Preview Scaling →
    Canvas**, or you can check a recording.
- **Centring and spacing are optical.** The goal is that something *looks* centred, or *looks* equally
  spaced, not that it measures so. Geometric centring is only the starting point.
  - Things that make a measured-centred layout look off:
    - a box's inner shadow or highlight, which moves the box's visible edge
    - glyph shapes: an "A" leans away from its neighbour, a "1" is narrow inside a tabular cell, and
      round letters overshoot
    - where the ink's weight sits
  - Measure first, then correct to what looks right at 1:1. The user's eye decides, so ask them to look
    at 1:1 and say which way it's off.
  - Put the correction in design pixels (so it scales with the meter), in a named constant, with a
    comment saying what it corrects and what was measured, and in
    `overlays\gmeter\design\README.md`.
  - Move in small steps. When one step looks off one way and the next off the other, split the
    difference (the live G landed halfway).
  - Example: the live G sits 0.5 design px above its window's box (the inner shadow) and 0.8 right of
    it.
- **Smoothness and timing:** use the plugin's 10 s stats in the OBS log, or frame-by-frame analysis of a
  recording.
  - **A whole recording:** `python -I vr_mirror\recording_analysis\analyse_recording.py
    "<recording>.mp4"`. It finds the recording's OBS log and frame trace, and measures the cockpit's
    jumps. It lines the video up with the trace, and lists the timing events (`summary.txt` in its
    output folder). Use it and extend it (`reference\recording_analysis.md`) instead of copying
    scripts from an earlier investigation.
  - Recordings are in OBS's recording folder (Settings → Output). The ones analysed so far are AV1,
    1920x1080, 90 fps. In the OBS log, `==== Recording Start` marks when one began. Log time minus that
    is the video time.
  - ffmpeg (with `ffprobe.exe` beside it) is needed. The scripts take the one `FFMPEG` names (the full
    path of `ffmpeg.exe`), else the one on PATH. If neither is there, ask the user where it is before
    downloading one.
  - Extract greyscale raw frames (`-ss <time> -t <s> -vf scale=960:540,format=gray -f rawvideo
    out.gray`). Measure them with numpy, which is installed (OpenCV isn't). Where a search over many
    shifts is too slow in Python, write a small C++ program and build it with MSVC's `cl`.
  - Data and scripts of analyses (frame traces, clips, scripts that line up a video with a trace) are
    kept outside the repo, in **the data folder**. That is the folder `SIMOPTIX_DATA` names, where
    `analyse_recording.py` writes by default (without it, beside the recording), with one folder per
    investigation and a README. Other files here call it "the data folder".
  - Image motion: compare patches of the cockpit (fixed in the headset's tracking space) between
    frames. A repeated game frame shows as the scenery moving exactly with the cockpit. The **frame
    trace** and **frame-number stamp** debug options (`vr_mirror\obs_plugin\README.md`, "Debug: stamp
    and frame trace") match a recording to the poses frame by frame.
- Report numbers, and say what wasn't checked (headset, live telemetry, a real recording).

## 5a. Replay tests from real sessions

A replay (`vr_mirror\e2e\replay\*.csv`, scenario `replay_<name>`) plays a real session back through the
fake runtime. It holds:
- the real head poses
- the runtime's real pose predictions and display slots
- every game frame at its real arrival time, and the slot each frame was really rendered for

Use one whenever a problem shows up in a real recording: make the replay, see it fail on the current
build, then fix.
`reference\e2e.md` ("Adding a replay of a real session") has the steps and the file format.

- **Record:** the user turns on the source's **Debug** setting (frame-number stamp and frame trace) and
  records. Ask for the video time of what they saw.
- **Use `vr_mirror\recording_analysis\`** (`reference\recording_analysis.md`) instead of writing new
  scripts. `episodes.py` lists what the plugin re-timed in a recording (from the OBS log and the trace,
  with video times and frame timing). `window.py` lines a stretch of video up with the trace by the
  stamp and says which lag the cockpit motion supports. Extend them rather than starting over.
- **Find the moment:** in the OBS log, the 10 s stats (repeats, game fps) and the "game behind" lines
  (frames judged and re-timed by the slot check). Video time is the log time minus `==== Recording
  Start`.
- **Line up video and trace** (`window.py` does this): decode the stamp (top-left, 15 squares of 16x16
  px, bit 0 first, white = 1) from each video frame. It is the `stamp` column of `_ticks.csv` and wraps
  every 32768 ticks (6 min), so pick the occurrence by time. Map frames by their own stamp, because OBS
  sometimes shows a frame twice.
- **Find the truth (the `lag` column)** (`window.py` does this): measure the cockpit's motion per
  frame, with patches on the roof, pillars and fixed panels. Leave out the wheel, screens, mirrors and
  OBS overlays, and pick new patches for each car. Compare the motion with the one that each candidate
  slot's pose predicts (lag 0, −1, −2, −3). The video decides, so don't assume. Frames that look normal
  are lag 0.
  - The lag is relative to the slot that the mirror app's arrival timing assigned at the time (the F
    rows' slot), so lag 0 means trusting that decision. When the bug is in the slot decision itself,
    the exported slot is the bug's output and the lag column must correct it. Otherwise the replay
    passes on the build it should catch (`reference\e2e.md`, "Adding a replay of a real session",
    step 3).
- **Export:** the trace's P, S and F rows of about 12 s around the moment, with times relative to the
  file start, and the lag per frame. Add a header that says where the replay comes from and what
  happened.
- **Test first:** run the new scenario on the current build, where it should fail. Check that it fails
  for the right reason (per-frame analysis of `vr_mirror\e2e\out\<scenario>\`), and that older builds
  it should catch fail too (`--tool <exe>`, built from that commit in a worktree). If the fake can't
  produce the failure, extend the fake (it can draw a detailed cockpit, `cockpit=1`, for this), and
  don't add hooks to the mirror app. Say so if it still can't.
- **Measures:** a replay is graded on **frames from the wrong pose**. That is exact: the mirror app's
  pose against the pose the fake rendered with, per frame. Replays are judged by poses alone, because
  their real thumbnails cover nearly the whole window. The synthetic scenarios also read the view on
  screen. Their colour codes are dithered, so that reading is good to about 0.05°.
- **Save the sources** to the data folder, as `<investigation>\` with a README:
  - the trace (zipped) and the OBS log
  - clips cut with `ffmpeg -c copy` around each analysed moment (they start at the keyframe before, so
    align by the stamp)
  - the analysis scripts and their results
  - for each session: recording, start time, first drawn tick, car, patches, finding

  Every replay in the repo has its sources there.
- **Pitfalls met:** for angles between nearly equal quaternions, use `2 atan2(|v|, |w|)` of the
  relative rotation and not `2 acos(dot)` (7-digit values read 0.05 deg apart). Never leave a shell's
  working directory inside `vr_mirror\e2e\out\<scenario>\`, because the runner deletes it and aborts.

## 5b. Check the overlays and race auto-record without the game

`shared\telemetry\games\lmu\fake\lmu_fake.exe` publishes data on LMU's telemetry interface, only while
LMU is closed, so the plugins read it exactly as they read LMU (through the shared reader,
`shared\telemetry\`).

- **The demo lap:** `lmu_fake.exe` plays the G-meter's 12 s demo lap for 60 s, which checks the meter's
  real telemetry path. To just watch the meter, use **Start demo** in its properties.
- **A race:** `lmu_fake.exe --race` also plays a race start and finish, for VR Mirror's race
  auto-record. Tick **Record races automatically** in the Pimax source and read the OBS log's "LMU race
  auto-record:" lines.

## 5c. Add a game to the telemetry

Add a folder under `shared\telemetry\games\` with code that implements `GameAdapter`
(`game_adapter.h`), one entry in `game_registry.cpp` (and its include), and its source file in
`shared\telemetry\sources.bat`. Nothing else changes. A game whose SDK can't be shipped is compiled only
when its SDK is found, and defines `HAVE_<GAME>` then. Check the game's terms for outside tools before
its support ships (Rules, "Other games").

## 5d. The OpenXR layer

Build it, run its check program, sign it, install it and test it with Easy Anti-Cheat as
`reference\openxr.md` says. Every change to the layer is signed again before it goes into the game,
because a rebuilt DLL is unsigned (Rules, "Never touch the game"). Sign it with the user's own
signing skill, and install it with `install.ps1`, because `install.bat` builds an unsigned one first.
Record what each start of the game showed in `findings\openxr.md`.

## 6. Commit

- Commit each finished change on its own, without being asked.
- The subject is `Area: what changed`. The body says why. An agent ends the message with the
  attribution line its own instructions give, if any.
- Commit with `git commit -s`, the maintainer and every agent alike. It adds a `Signed-off-by:` line
  with the name and email of the git config. That line certifies the Developer Certificate of Origin
  (`CONTRIBUTING.md`, "Sign-off"). An amend or a rebase keeps the line. To add it to the last commit,
  run `git commit --amend -s`. To add it to every commit of the branch, run `git rebase --signoff main`.
- Stage only the files the change touched, by path. Never stage build output, captures or local `.ini`
  files.
- In the same commit, update:
  - the matching README, when behaviour, settings or layout changed
  - the matching file of this skill, when a way of working changed
  - the part's findings file, when something was measured or an approach dropped
- Before committing docs, run `python -I .agents\skills\docs\check_docs.py` (the docs skill has the
  checklist).

## 7. Bring `main` up

- **Every change goes through a pull request on GitHub,** once the repo has its GitHub remote. Push the
  worktree's branch, open a pull request against `main` that says what changed, why and what was
  verified, and merge it on GitHub after its checks. Nothing is pushed to `main` directly.
- Until the remote exists, there are two cases:
  - If `main` isn't checked out anywhere, run `git push . <branch>:main` (fast-forward only).
  - If `main` is checked out in the main folder and that folder's tracked files are clean, run
    `git -C <main folder> merge --ff-only <branch>`. Otherwise leave it and tell the user the branch and
    that command.
