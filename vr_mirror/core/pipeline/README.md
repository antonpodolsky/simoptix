# The frame pipeline

The pipeline turns a game frame into the stabilized view. It has five stages, in the order the data
flows. Each stage is an interface with one or more implementations. They can be swapped, replayed
alone (`vr_mirror\e2e\stage_replay`) or mixed through the builder. The folder numbers give the order.

## Terms

| Term | Meaning |
|---|---|
| frame source | The code that delivers game frames, head poses and timing to VR Mirror: the Pimax source (`vr_mirror\frame_sources\pimax\`) or the OpenXR source (`vr_mirror\frame_sources\openxr\`) |
| drawn pose | The head pose a game frame was drawn with. Stage 2 of the pipeline answers it for each frame, and the code is named after it |
| mirror app | `simoptix_vr_mirror.exe`, VR Mirror in a desktop window (`vr_mirror\mirror_app\`). The e2e tests run it |
| picture room | How far a view can turn away from the pose of an eye image before the output would look past the image's edge and show black |
| observer | The debugging code beside the pipeline. It numbers the stamp and writes the frame trace |
| stamp | The frame number that VR Mirror draws as 15 small squares in the top-left corner of the view, when its Debug setting is on |
| frame trace | The CSV file that the Debug setting (or the mirror app's `SIMOPTIX_VR_MIRROR_TRACE`) writes, with one row per drawn frame: its poses and timing. With the Pimax source, Debug also writes a second file of head poses, predictions and arrivals |
| tick | One time OBS draws a frame of its output |
| arrival slot | The display slot a game frame is assigned from when it arrived, once the timing noise is removed. While the game keeps up, it is the slot the frame was drawn for |
| slot check | A way to find the drawn pose: it judges from thumbnails which display slot a frame was really drawn for |
| cockpit correction | A refinement of the drawn pose. It measures how the fixed cockpit moved between two frames and corrects the pose by what the head poses don't explain |
| host | A program that runs VR Mirror's pipeline and shows its result: the OBS plugin or the mirror app |
| look-ahead | The method behind Smooth and Smoothest. The view runs half a second or a second behind the game, and the smoothing plans a calm path from the head's movement in that time |

## The stages

| # | Stage | Folder | Interface | Implementations (file → class → config value) | Config field | Thread |
|---|---|---|---|---|---|---|
| 1 | Arrival timing | `1_arrival_timing` | `ArrivalTiming` | grid_arrival_timing → `GridArrivalTiming` → `kGrid`, reported_arrival_timing → `ReportedArrivalTiming` → `kReported` | `arrivalTiming` | The frame source's own thread |
| 2 | Drawn pose | `2_drawn_pose` | `DrawnPose` | slot_check_drawn_pose → `SlotCheckDrawnPose` → `kSlotCheck`, arrival_slot_drawn_pose → `ArrivalSlotDrawnPose` → `kArrivalSlot`, cockpit_drawn_pose → `CockpitDrawnPose` → `kCockpit`, reported_drawn_pose → `ReportedDrawnPose` → `kReported` | `drawnPose` | Fed on the copy device's thread (`DrawnPoseFeed`), asked on the output thread |
| 3 | Frame picker | `3_frame_picker` | `FramePicker` | fixed_delay_picker → `FixedDelayPicker` → `kFixedDelay` (the OBS plugin), newest_frame_picker → `NewestFramePicker` → `kNewestFrame` (the mirror app, Instant), held_frame_picker → `HeldFramePicker` → `kHeldFrame` (the mirror app, Smooth and Smoothest) | `framePicker` | Output thread |
| 4 | Smoothing | `4_smoothing` | `Smoothing` | instant_smoothing → `InstantSmoothing` → `kInstant`, lookahead_smoothing → `LookaheadSmoothing` → `kSmooth`, `kSmoothest` | `smoothing` | Output thread |
| 5 | Warp | `5_warp` | `Warp` | rotation_warp → `RotationWarp` → `kRotation` (plus `warp.hlsli` on the GPU) | `warp` | Output thread |

Each stage's README says what it does, its input and output, when to use each implementation, and
what it must not know.

Two parts sit beside the stages and are not stages:
- The **picture room** (`picture_room\`) is stateless geometry. Each smoothing uses it to keep its
  view inside the game's eye images. It computes the room a view has, and the stop at the edge that is
  each smoothing's last resort (`picture_room\README.md`).
- The **observer** (`observer\`, `Observer`, config field `observer` with the values `kNone` and
  `kStampTrace`) is for debugging. The glue hands it one record per output frame and the stages'
  report lines. It numbers the frame-number stamp and writes the frame trace (`observer\README.md`).

## The parts around the stages

- `pipeline_types.h` holds the plain data passed between stages (`ArrivalResult`, `FrameTimes`,
  `ViewGeometry`, `PreviousView`) and the helpers every stage uses the same way.
  - `TimesOf` settles a frame's times once: which arrival time it steps by, its pose time, the slot
    it is shown in, and the moment it stands for (that slot, or a steady rhythm).
  - `MomentTimes` does the same for an OBS tick's own moment.
- `pipeline_config.h` holds `PipelineConfig`, one enum per stage. `ConfigFor(settings)` maps the
  settings onto it:
  - `smoothing` 0 or 1 gives Instant (0 then shows the headset's view as it is), 2 gives Smooth and 3
    gives Smoothest.
  - `drawn_pose` 1 gives the slot check, 2 the cockpit correction and 0 the arrival slot.
  - A frame source that reports each frame's slot and pose (`FrameReport::kSlotAndPose`, the
    OpenXR layer) gets stage 1's and stage 2's reported methods, whatever `drawn_pose`
    says.
- `pipeline_builder.*` makes the stages.
  - `Make<Stage>(kind)` makes one stage. No stage sees `MirrorSettings`, and an
    implementation's tuning is its own constants.
  - `BuildPipeline` makes a fresh pipeline. It keeps the drawn pose stage while its value is
    unchanged, because a host's copy thread feeds it.
  - `ReconfigurePipeline` swaps only the stages whose value changed, so the others keep their
    state. Hosts call it once per output frame, which costs a comparison.
  - `SmoothingDelayS` gives a smoothing's delay.
- `stage_stats.h` holds `StatsReport`, the one call every stage reports its numbers through
  (`Report`: lines of a topic and name/value pairs since its last report), and `LineStatsReport`,
  which writes them as text lines. `Pipeline::Report` gathers every stage's lines. The OBS plugin's
  10 s log and the mirror app's console print them without knowing the stage or method. A frame
  picker that fixes its delay says so by a changed `OutDelay`, and the host logs it and then gives the
  picker's report.
- `pipeline.*` holds `Pipeline`, the glue for the output thread's stages (2's answers, 4 and 5).
  - It holds only what belongs to no stage: the step clock, the previous output, the smoothing
    window's buffers and the counters for traces. The smoothing alone decides the view.
  - Stage 1 runs on another thread, so the builder makes it and the host hands it to its frame
    source (`PimaxSource::SetArrivalTiming`).
  - Stage 2 is fed on the copy device's thread, so the host hands the pipeline's own stage 2 to its
    feed as well (`Pipeline::SharedDrawnPose`, `DrawnPoseFeed::SetDrawnPose`).

## One output frame (`Pipeline::Update`)

1. **The step clock:** the time since the last frame, by the frames' own spacing (`FrameTimes::step`)
   when synced, else by the poses' clock.
2. **Drawn pose:** the followed frame's pose, and the shown frame's pose when an older frame stands
   in.
3. **Smoothing** (one call, `Smoothing::Smooth`):
   - The glue samples the head's path that the smoothing asks for (`Window`). The samples lie around
     the moment that the drawn pose stage places the frame at (`MomentOf`: the slot the frame
     arrived for, or the measured head's own pose time), a display period apart. Each sample comes
     with the pose of the frame shown at that moment.
     - While each frame shows its own image, the poses are those of the moments' own slots
       (`PosesAt`).
     - When an older frame stands in for the moment (the OBS plugin's ticks), the poses lie as far
       after the moments as the shown frames run ahead of them (`PosesBetween`). The glue takes that
       lead from the frames' timing: the median over the last 9 ticks of the shown frame's moment
       minus the tick's (`MomentOf`), eased over 0.1 s (`Pipeline::ShownLead`).
   - The smoothing gives the view, inside the shown frame's image, by its own means (the picture
     room). Instant catches up, or the look-ahead plans, and then its own stop at the edge acts as a
     last resort. The glue counts the frames where the stop acted (`Smoothed::stopped`).
4. **Warp:** the shader's parameters for the shown frame's pose and the view, as the smoothing gave it.
5. **The observer's record of the frame** (debugging, off by default).

The poses are asked in the same order every frame: the drawn pose, the stand-in pose, the window's
heads, the window's predictions.

## Keeping stages separable

No stage includes another. The smoothing calls the picture room, which is stateless geometry beside
the stages. A stage gets its inputs as plain data and returns plain data, and nothing specific to one
method crosses an interface. A new implementation of a stage touches only its own files, plus one value
in `pipeline_config.h` and one `case` in `pipeline_builder.cpp`. `stage_replay` (`vr_mirror\e2e\`) runs
it alone or mixed with the others, and hosts follow `ConfigFor`.
