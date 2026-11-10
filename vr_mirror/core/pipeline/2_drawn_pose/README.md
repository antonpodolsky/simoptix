# Stage 2: drawn pose

This stage says which head pose each game frame was drawn with. The warp re-projects the frame's
image from that pose, and the view follows it. The stage also gives the poses of the frames shown
at moments around a frame, which the look-ahead keeps its path inside, and the moment a frame stands
for. How the stage finds the pose is each implementation's own business. A judged slot, a lag or a
correction never leaves the stage, so the stages after it can't tell which implementation ran.

## Terms

| Term | Meaning |
|---|---|
| arrival slot | The display slot a game frame is assigned from when it arrived, once the timing noise is removed. While the game keeps up, it is the slot the frame was drawn for |
| slot check | A way to find the drawn pose: it judges from thumbnails which display slot a frame was really drawn for |
| cockpit correction | A refinement of the drawn pose. It measures how the fixed cockpit moved between two frames and corrects the pose by what the head poses don't explain |
| slot lead | The runtime's next display slot minus the moment a frame arrived. It shifts when the game queues frames |
| thumbnail | A 64×32 colour image of the middle of an eye image. The slot check compares them |
| drawn pose | The head pose a game frame was drawn with. Stage 2 of the pipeline answers it for each frame, and the code is named after it |
| look-ahead | The method behind the Look-ahead smoothing. The view runs half a second behind the game, and the smoothing plans a calm path from the head's movement in that time |
| frame trace | The CSV file that the Debug setting (or the mirror app's `SIMOPTIX_VR_MIRROR_TRACE`) writes, with one row per drawn frame: its poses and timing. With the Pimax source, Debug also writes a second file of head poses, predictions and arrivals. With the OpenXR source, the second file holds the frames the layer published and how the game handed their images over |
| mirror app | `simoptix_vr_mirror.exe`, VR Mirror in a desktop window (`vr_mirror\mirror_app\`). The e2e tests run it |
| timing gate | The part of the slot check that lets a slot be judged only while the frames' timing shows a queue |
| lag | How many display slots earlier than its arrival slot the game drew a frame. 0 is on time. A lag appears while the game is behind |
| frame source | The code that delivers game frames, head poses and timing to VR Mirror: the Pimax source (`vr_mirror\frame_sources\pimax\`) or the OpenXR source (`vr_mirror\frame_sources\openxr\`) |
| patch | A small square (32×32 texels) cut from an eye image. The cockpit correction measures how patches move, and the e2e runner reads them from the captured window |
| host | A program that runs VR Mirror's pipeline and shows its result: the OBS plugin or the mirror app |

## Why it is more than "the prediction for the frame's slot"

When the game's GPU falls behind, the game queues up to two frames.
Each is still rendered with the pose predicted for its own slot, but it arrives one or two slots later
at the usual rhythm.
The arrival timing then takes it for an on-time frame, and its arrival slot's prediction is 11–22 ms
too new. The slot check finds the slot from the images. The cockpit correction then measures what is
left.

## Input and output

**In:** the stage is fed on the thread that owns the copy device
(`vr_mirror\core\adapters\d3d\drawn_pose_feed.*`) with:
- each new frame's record (`GameFrame`, in order: arrival, arrival slot, slot lead)
- the images it asked for (`Needs`):
  - a 64×32 colour thumbnail of the middle of the eye (`kThumbArea`)
  - patches of the eye, for the frames `Arrived` asks them for
- the runtime's predicted pose per slot (`PoseLookup`)
- the eye's geometry and the display period

On the output thread it is asked for a frame's times (`FrameTimes`) and the poses (`PoseSource`).

**Out:**
- `PoseOf`: the frame's drawn pose as known now. It can change as later frames are looked at, until
  the frame is shown.
- `PosesAt` and `PosesBetween` (between slots): the drawn poses of the moments of the head's path.
- `MomentOf`: where on the head's timeline a frame's pose sits. The look-ahead's window is centred
  there. By default it is the slot the frame arrived for (`FrameTimes::moment`), or the measured
  head's own pose time.
- The counts for the 10 s log lines.
- `Describe`, for the frame trace only (`pose_notes`, while a trace is written): how the frame's
  pose was found, as name/value pairs in the implementation's own terms.
  - The slot check gives `slots_before_arrival`, `judged` (1 for its own judgement in hindsight,
    0 for the last frame's) and `own_judgement` (before hindsight).
  - The cockpit correction adds `looked_at`, `corrected`, `against_slots_before_arrival` and
    `correction_deg`.

  Only this stage and the trace see these terms.

## Implementations

| File | Class | Config value | When to use |
|---|---|---|---|
| `slot_check_drawn_pose.h/.cpp` | `SlotCheckDrawnPose` | `DrawnPoseKind::kSlotCheck` (`drawn_pose` 1, the mirror app's default) | The slot check. It takes Pimax's prediction for the slot each frame was really rendered for, judged from the images. For each pair of candidate slots of two consecutive frames, it re-projects both thumbnails to the pose halfway between those slots' poses and compares them. A forward filter with change penalties picks the slot, only while the timing gate sees a queue. A frame's slot is judged again in hindsight as later frames arrive |
| `slot_check_drawn_pose_lag.h/.cpp` | `ReprojectionError`, `RotationSensitivity`, `TimingGate`, `LagFilter` | none | The slot check's helpers: the measures, the timing gate and the lag filter. `vr_mirror\recording_analysis\replica.py` mirrors them in Python |
| `arrival_slot_drawn_pose.h/.cpp` | `ArrivalSlotDrawnPose` (and `PredictedPose`) | `DrawnPoseKind::kArrivalSlot` (`drawn_pose` 0) | Pimax's prediction for the slot each frame arrived for, which is right while the game keeps up. It looks at no images and makes no thumbnails. Use it as a comparison, or without the GPU's thumbnails. `PredictedPose` is how the slot check's kinds turn a slot into a pose |
| `reported_drawn_pose.h/.cpp` | `ReportedDrawnPose` | `DrawnPoseKind::kReported` (a frame source whose frames report their slot and pose: the OpenXR source) | The pose recorded for the slot the frame says it was drawn for (`FrameTimes::drawnSlot`), exactly, whichever slot it is shown in. It looks at no images and makes no thumbnails |
| `cockpit_drawn_pose.h/.cpp` | `CockpitDrawnPose` | `DrawnPoseKind::kCockpit` (`drawn_pose` 2, the OBS plugin's) | The slot check, run unchanged inside, plus a correction of each frame's pose measured from its fixed cockpit while frames arrive irregularly (below) |
| `cockpit_drawn_pose_shift.h/.cpp` | `MeasurePatchShift`, `FitCorrection` | none | The cockpit correction's helpers: the patch grid, one patch's move between two frames, and the rotation most patches agree on |

`ConfigFor(settings)` picks the implementation from `drawn_pose`. The OBS plugin sets 2, or 0
when stabilization is off.

The builder makes the stage shared (`MakeDrawnPose`). The pipeline asks it, and the host hands the
same object to its feed (`Pipeline::SharedDrawnPose`, `DrawnPoseFeed::SetDrawnPose`, and for
the OBS plugin `FrameRing::SetDrawnPose`). The feed makes the images on the GPU and reads them back
without flushing.
- A rebuilt pipeline keeps the stage while its value is unchanged (`BuildPipeline`), so it keeps its
  history.
- A new stage starts afresh. Until it has looked at a frame, the frame gets its arrival slot's pose.

When a host draws a frame before its own images were read, it gets the slot check's latest judgement.
The mirror app at Instant draws at arrival and waits up to 3 ms for a frame's own patches. The OBS
plugin draws frames after their images were read, so this hardly ever applies there.

**Thread:**
- The stage is fed on the thread that owns the copy device (OBS's copy thread, `FrameRing`, or the
  mirror app's main thread).
- It is asked on the host's output thread (OBS's graphics thread, the mirror app's main thread).
- The slot check's and the cockpit correction's judgements are shared between the two threads under
  their own lock. The lock is held only to copy a few values, and the reprojections and the patch
  measuring run outside it.

**Uses:** plain data (frame records, thumbnails, patches, poses) and its own lock.

**Must not know:** D3D, PVR, threads (beyond its own lock), how a host shows frames, or the stages after it.

## The cockpit correction (`CockpitDrawnPose`)

**Why.** The slot check chooses between whole slots from 64×32 thumbnails. When the game falls below
the headset's refresh rate, it chooses wrong often enough to make the view jump, and every wrong slot
stays. The cockpit is fixed in the headset's tracking space (a sim rig). So between two frames it
moves only as the poses they are shown with say. Whatever it moves beyond that is the frame's pose
error. The correction measures that error as a small rotation (yaw, pitch and roll), not as a choice
of slot.

**How, per frame:**

1. The slot check places the frame as usual (its lag, its slot's predicted pose).
2. While frames arrive irregularly, the feed makes 40 grey patches of 32×32 texels (about 4×4 game
   pixels each, 0.18°) from the frame's copy. Irregular means a dropped or bunched frame, a slot
   jump, a skipped serial, or the slot check's timing gate.
   - 20 patches are the areas of a 5×4 grid over the eye (`kCockpitAreas`).
   - The other 20 lie where the poses say the previous frame's grid content went (`Arrived`). A
     turning head moves it by up to 20 texels a frame.
   - They are read back like the thumbnails, with `DONOTFLUSH` queries and nothing allocated per frame.
3. Each moved patch is aligned with the previous frame's.
   - A coarse search at half resolution finds the move within 10 texels.
   - Then a Gauss-Newton fit runs on both patches sampled half the move apart, so both get the same
     interpolation blur. It uses every other texel. Texels that don't match count less (Cauchy
     weights), so the part of a patch that moves on its own drops out.
   - A patch that is too plain, has too little of it matching, or is out of reach is not measured.
4. The stage finds the rotation most patches agree on: least median of squares over pairs of patches,
   then a fit weighted by each patch's precision.
   - Patches that kept disagreeing over the last half second are left out. An example is scenery
     moving slowly past a pillar's edge.
   - A step larger than what the runtime's predictions move over six slots is not taken, because
     something else moved.
5. The image decides from frame to frame and the head poses decide in the long run, like a phone's
   gyroscope and compass.
   - The correction is pulled back to the slot check's pose with a time constant of 0.3 s, at most
     3°/s (faster only beyond what the predictions make plausible).
   - It fades in and out over 0.3 s, and goes after 0.5 s of regular frames.
   - A frame it can't measure keeps the last correction, pulled back.

The correction is applied after the slot's predicted pose, inside `PoseOf`. A corrected frame keeps
the slot it was corrected against, not the slot check's later hindsight. While the game keeps up with
the headset, it makes no patches and corrects nothing. The correction is then the identity, which
`PredictedPose` leaves out entirely. With the default setting, `stage_replay --golden` is byte for
byte main's.

**Cost:** 0.36 ms of the copy thread per frame while it measures (20 patch pairs), plus 40 small tiles
on the GPU from the frame already copied. It costs nothing while frames arrive regularly. The OBS
plugin logs the cost every 10 s ("cockpit correction: ..."). The time there is all of `Look` on frames
with patches, the slot check's judgement included.

The moment a frame stands for is the slot it is shown in, not the earlier one a late frame was drawn
for. So a re-timed frame's view doesn't jump back along the path. When the OBS plugin makes a new D3D
device (each time the source is shown), the feed makes its images afresh with it.
