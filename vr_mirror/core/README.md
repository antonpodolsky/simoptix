# core

The core holds the logic that every VR Mirror host shares: the OBS plugin (`vr_mirror\obs_plugin\`)
and the mirror app (`vr_mirror\mirror_app\`). The end-to-end tests (`vr_mirror\e2e\`) drive it
directly. It is independent of OBS, windows, swap chains, UI, and where the frames come from. The
game frames and head poses come from a frame source (`vr_mirror\frame_sources\`). Logging and the CPU
counters are in `shared\`, because the Overlays plugin uses them too.

## Terms

| Term | Meaning |
|---|---|
| game eye | The eye image a frame source delivers. The Pimax source's is a half-size copy of the left eye (2508×2084 in LMU at Render Quality 1.0). The OpenXR source's is the left eye at full size (5084×4168 in LMU) |
| drawn pose | The head pose a game frame was drawn with. Stage 2 of the pipeline answers it for each frame, and the code is named after it |
| display slot | One of the regular moments, 90 per second, at which the headset shows a new image. The game draws each frame for one slot, with the head pose the runtime predicted for that slot |
| frame source | The code that delivers game frames, head poses and timing to VR Mirror: the Pimax source (`vr_mirror\frame_sources\pimax\`) or the OpenXR source (`vr_mirror\frame_sources\openxr\`) |
| slot check | A way to find the drawn pose: it judges from thumbnails which display slot a frame was really drawn for |
| stamp | The frame number that VR Mirror draws as 15 small squares in the top-left corner of the view, when its Debug setting is on |
| frame trace | The CSV file that the Debug setting (or the mirror app's `SIMOPTIX_VR_MIRROR_TRACE`) writes, with one row per drawn frame: its poses and timing. With the Pimax source, Debug also writes a second file of head poses, predictions and arrivals |
| host | A program that runs VR Mirror's pipeline and shows its result: the OBS plugin or the mirror app |

## Structure

The core has three parts. Dependencies point inwards:
- `pipeline\` is the frame pipeline. Its stages and their order are in `pipeline\README.md`.
- `common\` holds the plain data and logic that the pipeline and the adapters share.
- `adapters\` holds the thin glue for input and output that the pipeline runs on.

Each stage folder has its own README. It lists what the stage does, its input and output, its
implementations (file, class, config value, when to use), its thread, and what it must not know.
The rules for includes, file names and the build lists are in the dev skill
(`.agents\skills\dev\SKILL.md`, "Code structure").

## Folders and files

| Folder or file | What |
|---|---|
| `common\` | Plain data, maths and logic shared by the pipeline and the adapters. No input or output, no threads |
| `common\rot.h` | Quaternion maths (`Quat`, `Slerp`, `LookRotation`, `ToMatrix`, small rotations as rotation vectors) |
| `common\game_frame.h` | A game frame's record (`GameFrame`) and the game eye's geometry (`EyeInfo`) |
| `common\settings.h` | VR Mirror's settings (smoothing, zoom, the drawn pose method), with `VisitSettings` for loading, saving and the UI |
| `common\pose_source.h` | Where the pipeline gets head poses and the runtime's predictions per display slot. The interface is implemented by `PimaxSource` live and by recorded poses in `vr_mirror\e2e\stage_replay` |
| `common\pose_history.h` | The recent head poses and the predictions per display slot, with their lookups: a slot's own, or interpolated between slots (logic) |
| `common\clock_sync.h` | The offset between the Pimax runtime's clock and the performance counter (logic) |
| `common\arrival_stats.h` | The game's frame rate over 3 s windows and the timing noise the arrival timing removed, for the log (logic) |
| `common\footprint.h` | Counters for the 10 s footprint stats: call counts and times, per-tick time quantiles, CPU share (logic) |
| `pipeline\pipeline_types.h` | The plain data the stages pass to each other (`FrameTimes`, `ViewGeometry`, `PreviousView`, ...) and the shared helpers (`TimesOf`, `GeometryFor`, `ViewToEye`, `LevelHorizon`) |
| `pipeline\pipeline_config.h` | `PipelineConfig`: which implementation each stage uses, one field per stage. Also `ConfigFor(settings)` and each value's name (`NameOf`, `Parse`) |
| `pipeline\pipeline_builder.*` | `Make<Stage>(kind)`, `BuildPipeline`, `ReconfigurePipeline` and `SmoothingDelayS`. This is the only code that knows every implementation |
| `pipeline\stage_stats.h` | `StatsReport`, the one call every stage reports its numbers through (topic, name/value pairs), and `LineStatsReport`, which writes them as log lines |
| `pipeline\pipeline.*` | `Pipeline`: the glue on the output thread. It runs stage 2's answers and stages 4 and 5 for each output frame, and keeps the step clock, the previous output and the head's path for the smoothing. It never changes the smoothing's view |
| `pipeline\1_arrival_timing\` | Stage 1: `arrival_timing.h` (interface), `grid_arrival_timing.*` (`GridArrivalTiming`), `reported_arrival_timing.*` (`ReportedArrivalTiming`) |
| `pipeline\2_drawn_pose\` | Stage 2: `drawn_pose.h` (interface, `PoseLookup`, `kThumbArea`, `PictureNeeds`), `slot_check_drawn_pose.*` (`SlotCheckDrawnPose`) with its helpers `slot_check_drawn_pose_lag.*`, `arrival_slot_drawn_pose.*` (`ArrivalSlotDrawnPose`, `PredictedPose`), `cockpit_drawn_pose.*` (`CockpitDrawnPose`: the slot check plus the pose correction from the cockpit) with its helpers `cockpit_drawn_pose_shift.*`, and `reported_drawn_pose.*` (`ReportedDrawnPose`: the pose of the slot a frame source says the frame was drawn for) |
| `pipeline\3_frame_picker\` | Stage 3: `frame_picker.h` (interface), `fixed_delay_picker.*` (`FixedDelayPicker`, `WarmupSlots`), `newest_frame_picker.*`, `held_frame_picker.*` |
| `pipeline\4_smoothing\` | Stage 4: `smoothing.h` (interface), `instant_smoothing.*` (with `OrientationFilter`), `lookahead_smoothing.*` (Smooth and Smoothest, with the path solver `LookaheadSmoother`) |
| `pipeline\picture_room\` | Beside the stages: `picture_room.*`, the stateless geometry each smoothing uses to keep its view inside the game's eye images (the room a view has, the stop at the edge) |
| `pipeline\observer\` | Beside the stages: `observer.h` (interface, `FrameRecord`, `TraceSink`), `no_observer.h` (`NoObserver`), `stamp_trace_observer.*` (`StampTraceObserver`: the frame-number stamp and the frame trace) |
| `pipeline\5_warp\` | Stage 5: `warp.h` (`WarpParams`, interface), `rotation_warp.*` (`RotationWarp`), `warp.hlsli` (the per-pixel warp that a host wraps in its own shader or effect) |
| `adapters\` | Thin glue for input and output that the pipeline runs on. It makes no decisions |
| `adapters\d3d\frame_source.h` | `FrameSource`: what a host needs from a frame source (`vr_mirror\frame_sources\`): the frames, the poses, the eye, the clock, and a copy of each eye image into the host's store |
| `adapters\d3d\frame_copy.*` | A host's own copy of a frame's eye image: the texture (shared or not, copied or drawn into) and its views. The frame source fills it |
| `adapters\d3d\thumbnail.*` | Tiny thumbnails for comparing images by content, or several areas side by side (the cockpit patches), read back without flushing |
| `adapters\d3d\drawn_pose_feed.*` | `DrawnPoseFeed`: makes the images stage 2 looks at (thumbnails and the cockpit patches it asks for) on the GPU, reads them back, and feeds them to stage 2 with the frame source's pose predictions |
| `adapters\system\trace_writer.*` | Debug trace files, written on their own thread (the frame trace) |
| `build\common.rsp`, `pipeline.rsp`, `adapters.rsp` | The `.cpp` files of each part, as `cl` response files that the build scripts list |
| `build\embed_text.ps1` | Build helper that embeds `warp.hlsli` into a host as a string |
