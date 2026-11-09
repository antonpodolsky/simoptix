# Adding an implementation of a pipeline stage

Terms: `reference\terms.md`. The stages and the parts around them: `vr_mirror\core\pipeline\README.md`.
The rules that keep stages swappable: `SKILL.md`, "1a. Keep every stage swappable".

## Steps

1. **Its files.** Add `<what_it_does>_<stage>.h/.cpp` to the stage's folder, deriving from the stage's
   interface (for example `slot_check_drawn_pose.*`). Name its private helpers, if any,
   `<what_it_does>_<stage>_<helper>.*`.
   - Its tuning is its own constants, in its own files. No stage takes `MirrorSettings`.
   - Its numbers go out through `Report` (`pipeline\stage_stats.h`).
2. **Its config value.** Add a value to the stage's enum in `pipeline_config.h`, with its name in
   `NameOf` and `Parse`.
3. **Its line in the builder.** Add one `case` to `Make<Stage>` in `pipeline_builder.cpp`, and list the
   `.cpp` in `vr_mirror\core\build\pipeline.rsp`.
4. **Try it.** Run it alone, or mixed with the other stages, in `vr_mirror\e2e\stage_replay`
   (`--<field> <value>`: the flags parse every value by name, so they need no change). Hosts follow
   `ConfigFor`, which only the settings change.
5. **Document it.** Add it to the stage's README table. Record what it measured in
   `findings\pipeline.md`, and why, if it is dropped.

To make it the one a host uses, change the host's settings mapping (`ConfigFor`). A default changes only
on measurements that show it is better (`SKILL.md`, "1a. Keep every stage swappable").

## What a new method touches, per stage

Checked against the code before the first public version. "Its files" are the new `.h/.cpp` and its row in the stage README's table.
Every row also adds one line to `vr_mirror\core\build\pipeline.rsp`.

| Stage | Its files | `pipeline_builder.cpp` | `pipeline_config.h` | Nothing else, because |
|---|---|---|---|---|
| 1 arrival timing | Yes | One `case` | One value, and its name | `PimaxSource` holds it as `ArrivalTiming` |
| 2 drawn pose | Yes | One `case` | One value | Hosts feed it through `DrawnPoseFeed`, and it asks for its images itself (`Needs`, `Arrived`). The glue asks `PoseOf`, `PosesAt` and `MomentOf`. Its numbers go out through `Report`, and the plugin logs its name from `NameOf` |
| 3 frame picker | Yes | One `case` | One value | Hosts ask `Tick`, `OutDelay` and `SlotsInUse`. Its numbers go out through `Report` |
| 4 smoothing | Yes | One `case` | One value | The glue calls `Window` and `Smooth`, and it keeps its view inside with the image room's functions. A smoothing with a delay of its own also needs its line in `SmoothingDelayS`, because hosts size their hold by it before a pipeline exists |
| 5 warp | Yes | One `case` | One value | The glue asks `Params`. The GPU half (`warp.hlsli`) is shared by both hosts' shaders, so a warp that changes the shader also changes `warp.hlsli` |
| observer | Yes | One `case` | One value | The glue hands it records and report lines. Hosts give it a sink and draw `Stamp` |

## What keeps stages separable

- No stage includes another. The smoothing calls the image room, which is stateless geometry beside
  the stages.
- A stage gets its inputs as plain data and returns plain data.
- Nothing specific to one method crosses an interface.
- Numbers that depend on order (levelling, pose lookups) stay where they are documented. For example,
  each smoothing levels its own view (`vr_mirror\core\pipeline\4_smoothing\README.md`), and the glue
  asks the poses in a fixed order every frame (`vr_mirror\core\pipeline\README.md`, "One output
  frame").
