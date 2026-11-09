# Running and extending the e2e suite

Terms: `reference\terms.md`. What the suite is, its scenarios and what it checks:
`vr_mirror\e2e\README.md`. The numbers to compare a run with: `reference\e2e_results.md`. What was found
with it: `findings\e2e.md`.

## Running the suite

Build `vr_mirror\mirror_app\build.bat` first, because the runner tests whatever
`vr_mirror\mirror_app\simoptix_vr_mirror.exe` currently is. Then:

```
vr_mirror\e2e\build.bat
```

It builds the fake runtime (`bin\libPVRClient64.dll`), the fake OpenXR game, `bin\e2e_runner.exe` and
`bin\stage_replay.exe`.

```
vr_mirror\e2e\bin\e2e_runner.exe
```

It runs every scenario. Name some to run only those (`e2e_runner.exe stabilization black_borders`), or
list them with `--list`.

- Each scenario opens a small window (320×180) on top of everything, for about 15 s.
- With every scenario at all three smoothings, that is 54 runs. They took 787 s (about 13 minutes) on
  2026-11-08.
- Don't use the PC meanwhile. For consistent timing, close LMU, the mirror app, Pimax Play and other
  heavy apps first.

| Option | What it does |
|---|---|
| `--tool <exe>` | Tests another build, for example one made with `vr_mirror\mirror_app\build.bat simoptix_vr_mirror_dev.exe` while `simoptix_vr_mirror.exe` is running |
| `--set key=value` | Sets a mirror app setting for every scenario (`--set zoom=1.3`), with the keys of its ini (`vr_mirror\mirror_app\README.md`, "Settings"). A scenario's own settings take precedence |
| `--fake key=value` | Sets a fake runtime setting for every scenario (`--fake drawn_fov=0.96`, "Tuning the fake", below). It takes precedence over the scenario's own |
| `--output WxH` | Sets the window size for every scenario. The default is 320x180: small, and it reads the same as larger windows (`reference\e2e_results.md`, "Window size") |
| `--background` | Stops keeping the window on top |
| `--list` | Lists the scenarios |

For each scenario, `out\<scenario>\` keeps:
- `track.csv` (synthetic scenarios: the seen, intended and eye direction per frame)
- `tool_frames.csv`, `captures.csv`, `tool.log`, `fake.log` and `scenario.cfg`

The mirror app logs every stage's numbers every 10 s while it runs, like the OBS plugin, in
`out\<scenario>\tool.log`. An example is the slot check's "game behind: X frames rendered for an
earlier slot ..., Y frames judged by picture".

## The local files test

What it runs and checks: `vr_mirror\e2e\README.md`, "The local files test".

1. Build both plugins, the OpenXR layer (`vr_mirror\frame_sources\openxr\build.bat`) and
   `vr_mirror\e2e\build.bat`. The test runs the DLLs those builds left in their `bin\` folders.
2. Close OBS and LMU. The test refuses to start while either runs, because its OBS would share the
   plugins' memory with the other.
3. Run `vr_mirror\e2e\bin\local_files_e2e.exe`. The first run copies the installed OBS into
   `vr_mirror\e2e\out\obs_portable\` (466 MB). `OBS_DIR` names another OBS folder to copy.
4. Each case's files are in `vr_mirror\e2e\out\local_files\<case>\`: the test `%LOCALAPPDATA%` (the
   folder `appdata`), OBS's log (`obs.log`) and the fakes' logs.

A change to a kind of file, or a new kind, gets a case or a check in `local_files_e2e.cpp` that fails
on the build before the change.

## The stage replay

`stage_replay` runs core's frame pipeline offline. `vr_mirror\core\pipeline\pipeline_builder.cpp` builds
the pipeline for each run's settings, and the run needs no mirror app, fake runtime or GPU. It is
deterministic.

**Its inputs.** Four of the five replays (all but `late_frame_same_slot`), and synthetic motion.
- In the replays, each frame is drawn with its true slot's pose, as the slot check would find it at
  best (`replay_drawn_pose.h`).
- The synthetic motions are `edge_test+heartbeat` and `racing+heartbeat`, with exact pose predictions,
  about 3% dropped frames and a few unsynced ones.
- Poses and predictions are fed to core's pose history in time order, up to the moment a host draws, as
  the mirror app does live.
- `--replays DIR` reads the replays from another folder.

### One stage on real inputs: `--stage drawn_pose`

```
vr_mirror\e2e\bin\stage_replay.exe --stage drawn_pose replay\<name>.csv [--show FIRST-LAST] [--dump FILE] [--delays A,B,...] [--drawn_pose K]
```

It replays only the drawn pose stage, on a replay's real thumbnails, slots and pose predictions. The
stage is the slot check (`vr_mirror\core\pipeline\2_drawn_pose`), or another one with
`--drawn_pose K`.
- It scores the stage's poses against the true ones. A pose is wrong when it is more than 0.01° off.
- The score is given for each way a host asks for poses:
  - the mirror app at Instant (the previous frame's decision)
  - the OBS plugin at Instant (the frame's own decision)
  - in hindsight, half a second and a second later (Smooth and Smoothest). `--delays` names other
    delays.
- `--show` prints the evidence per frame. `--dump` writes it all, for trying decision rules offline.
- It takes about a second. Use it to find out why a replay fails, and to try changes to the slot check.
  The e2e suite still decides.

Until 2026-10-30 this was a program of its own, `lag_sim`, then `--stage frame_matching`, and until
2026-11-08 `--stage picture_pose`.
`findings\e2e.md` calls it `lag_sim`. Until 2026-10-31 it scored lags, with the same numbers.

### The whole pipeline: `--pipeline`

```
vr_mirror\e2e\bin\stage_replay.exe --pipeline [--smoothing smooth --drawn_pose cockpit ...] [--zoom 1.2] [--ticks 60] [--input NAME ...]
```

It runs the whole pipeline with any mix of stages. The settings' configuration (Smoothest, the slot
check) applies, with any stage replaced by its flag, named after the config's field and value:

| Flag | Values |
|---|---|
| `--arrival_timing` | `grid`, `reported` |
| `--drawn_pose` | `slot_check`, `arrival_slot`, `cockpit`, `reported` |
| `--frame_picker` | `fixed_delay`, `newest_frame`, `held_frame` |
| `--smoothing` | `instant`, `smooth`, `smoothest` |
| `--warp` | `rotation` |
| `--observer` | `none`, `stamp_trace` |

- `--zoom Z` sets the zoom.
- The run draws as the mirror app's window, or with `--ticks HZ` as the OBS plugin's ticks, with the
  frames picked by the frame picker.
- It runs over the replays and motions. `--input NAME` picks some.
- The slot check and the cockpit correction are given the replays' true slots' poses (`arrival_slot`:
  the slot each frame arrived for). To replay them on the real thumbnails, use `--stage drawn_pose`.

It scores each input from the pipeline's numbers alone (`scores.*`). Lower is better in every measure:

| Measure | What it counts |
|---|---|
| black | Outputs where a corner of the window, warped as `warp.hlsli` does, falls outside the shown image |
| lag | How far the view's look direction trails the image's, p99 and max |
| jitter still, jitter small | What is left of the view's direction after a local quadratic fit over ±50 ms (as e2e measures it), rms, while the image turns under 5°/s and at 5–30°/s |
| edge limit | Outputs where the smoothing's last resort stopped the view at an edge of the shown image. Before 2026-11-01 it was the clamp after the smoothing, against the followed image only |
| planned | Outputs where the planning pulled the path in |
| wrong pose | Outputs warped from a pose more than 0.01° off the one the frame was really drawn with (the replays' true lags, and the motions' exact predictions) |

The baseline of these scores is in `reference\e2e_results.md`, "Stage replay baseline". The whole run
takes about 10 seconds.

### Every output: `--golden` and the refactor guard

```
vr_mirror\e2e\bin\stage_replay.exe --golden out\stage_replay\golden_new.txt
```

It writes every output of the pipeline, each float exactly in hex: the 28 numbers of the warp, the head
pose, the view and image pose, and the edge counters. It runs over a fixed matrix:
- the six inputs, at Instant, Smooth and Smoothest, and at zoom 1.2 and 1.5
- drawn as the mirror app's window (at arrival, or held for the look-ahead delay), and as the OBS
  plugin's 60 Hz ticks (each tick's own moment, with an older frame standing in as the image, which e2e
  never sees)
- then single runs with stabilization switched off and on

That is 78 runs and about 40 MB.

**Refactor guard.** A change that must not change behaviour (moving code, splitting the pipeline into
stages) must reproduce the build before it byte for byte. Write `golden_main.txt` with that build, then
compare with `fc /b` or `cmp`. A difference is a behaviour change until shown otherwise. The e2e suite
still decides at the end.

### Stage 1 and stage 3 over OBS traces

```
vr_mirror\e2e\bin\stage_replay.exe --stage arrival_timing [--arrival_timing K] [--trace TICKS PIMAX ...]
```

It runs the arrival timing alone over the traces' frame arrivals. It reports the noise it removes, how
far the de-jittered arrivals' spacing stays off the display grid, and how often its slot agrees with the
one the plugin recorded.
- The grid on 2026-10-22 14:26: 0.487 ms rms removed, 0.031 ms off the grid against the plugin's 0.026,
  100% agreement.
- On 2026-10-21 21:49: 0.708 ms, 0.065 against 0.062, 99.99%.
- The small differences come from the plugin's session restarts, which the run doesn't see.

```
vr_mirror\e2e\bin\stage_replay.exe --stage frame_picker [--frame_picker K] [--trace TICKS PIMAX ...]
```

It runs the frame picker as the OBS plugin does: the fixed delay, unless `--frame_picker` names another.
- **Its inputs:** OBS frame traces, each an `_ticks.csv` with its `_pimax.csv` (the ticks and the frame
  arrivals), and synthetic ticks at 50, 59.94, 60 and 144 Hz. The traces are in the data folder's
  `2026-10 late frames after drops\trace\`.
- **In the synthetic runs** the smoothing changes mid-run, copies finish after a seeded random 1–3 ms
  (5% of them after 5–30 ms), and the store is resized as the picker asks.
- **It prints** the picks by kind per run and the delay it fixed, how far each tick's moment trails the
  shown image's slot (median), and the largest jump of the moments apart from the ticks' own step
  (without a smoothing change).
- When the plugin's picking became this stage, it was checked identical tick by tick against a verbatim
  copy of the plugin's earlier code: the entry shown, the kind of pick, the output delay, the moment,
  the tick the delay is fixed, and the entries in use. There were 0 differences on the traces of
  2026-10-22 14:26 and 2026-10-21 21:49 (457,913 ticks) at each smoothing, and on the synthetic ticks.

## Adding a replay of a real session

1. **Record.** In OBS, tick the Pimax source's **Debug** setting (the stamp and the frame trace), and
   record.
2. **Export the moment.** Find the moment in the recording, and match its stamped frame numbers to the
   trace's `_ticks.csv`. Cut the matching rows of `_pimax.csv` into a file in `vr_mirror\e2e\replay\`.
   The format is in the header of `queued_late_frames.csv`. `vr_mirror\recording_analysis\` does these
   steps (`episodes.py`, `window.py`, `export_replay.py`: `reference\recording_analysis.md`).
3. **Set the lag column.** Each frame's `lag` is the slot it was really rendered for, relative to the
   one the mirror app assigned. It comes from matching the recording's cockpit motion against the
   predictions for each slot. Frames that look normal have lag 0.
   - The file's `slot` is what the mirror app's arrival timing decided at the time, not anything the
     game reported. Lag 0, the export's default, means trusting that decision.
   - So where the bug is in that decision itself (`late_frame_same_slot`: two frames given one slot),
     the exported slot *is* the bug's output, and only the lag column puts the truth in.
   - Set the lag from the video (`window.py`, or the head pose step between the slots against the jolt
     measured). Never set it from the trace's own slots, or the replay passes on the build it should
     catch.
4. **Make its real thumbnails.** `vr_mirror\recording_analysis\export_thumbs.py` writes
   `<name>.thumbs.png` next to the replay. A replay without them fails, with a note saying so.
5. **Name it** after the issue it guards against, not after the race situation, which goes in its
   header.
6. **Show that it fails** on a build that has the issue, before calling it a test.

Every CSV in `vr_mirror\e2e\replay` becomes a scenario `replay_<name>`.

The source data of every replay is too big for the repo. It is kept in the data folder's `2026-10 late
frames after drops\`: the session's full frame trace, its OBS log, a clip of the moment, and the script
that fitted the lag column. Its README says how video and trace line up, per test. Save a new replay's
sources there too (`SKILL.md`, "5a. Replay tests from real sessions").

## Head motion

A scenario's motion spec combines parts with `+`, for example `racing+heartbeat`. The parts are `still`,
`heartbeat`, `head_tremor`, `racing`, `glances` and `edge_test`
(`vr_mirror\e2e\fake_game\fake_game_motion.h`). `glances` is racing's sway and glances, easing in and
out, without its road vibration.

## Tuning the fake

`out\<scenario>\scenario.cfg` holds the fake runtime's settings. Scenarios can override these:

| Setting | Default | What it does |
|---|---|---|
| `refresh_hz` | 90 | The display's rate |
| `slot_step_lead_ms` | 1.0 | The predicted slot steps this long before each vsync |
| `arrival_phase_ms` | 0.2 | Frames arrive this long after a vsync, on average |
| `arrival_jitter_ms` | 0.5 | The standard deviation of the arrival time |
| `drop_rate` | 0 | The share of slots the game misses |
| `replay`, `thumbs` | none | Set by replays |
| `game_start_s` | 0 | The game sends no frames for that long after the mirror app switches the mirror on. The texture and frame signal are published, and the signal is low. The calls made meanwhile are logged (`idle_calls` in `fake.log`) |
| `drawn_fov` | 1 | Draws only that fraction of each reported field-of-view tangent, and black beyond it. So the game image is smaller than the mirror app assumes, and shows as black borders. Set it with `--fake` |
| `cockpit` | 0 | Draws a car's interior instead of the direction codes (below the table) |
| `queue_lag` | 0 | Lets the game fall up to that many slots behind, with `drop_rate` (below the table) |

- **`cockpit`:** the interior is fixed in the tracking space (a sim rig). It has detail of 0.5–3° on the
  roof, pillars and dashboard, a plain dark panel, a wheel that turns with the steering, and the world
  outside the windows turning as the car corners (up to 40°/s).
- **`queue_lag`:** after a dropped slot the game often stays a slot further behind, and catches up now
  and then. Its frames arrive 4 ms later while behind, which is the slot lead's shift the timing gate
  looks for. `fake.log` gives each frame's lag.
