# Stage 4: smoothing

This stage chooses the view to show for an output frame. The camera follows the head pose of the
frame's image, with tremor, vibration and tracking noise smoothed away. The view is level.
Levelling stays inside each smoothing, because its path is checked against the images after
levelling, and levelling a level view again changes numbers in float. The smoothing keeps the view
inside the game's eye images by itself, so no black edge shows. The view it gives is the view that is shown.

## Terms

| Term | Meaning |
|---|---|
| drawn pose | The head pose a game frame was drawn with. Stage 2 of the pipeline answers it for each frame, and the code is named after it |
| picture room | How far a view can turn away from the pose of an eye image before the output would look past the image's edge and show black |
| look-ahead | The method behind the Look-ahead smoothing. The view runs half a second behind the game, and the smoothing plans a calm path from the head's movement in that time |
| host | A program that runs VR Mirror's pipeline and shows its result: the OBS plugin or the mirror app |
| mirror app | `simoptix_vr_mirror.exe`, VR Mirror in a desktop window (`vr_mirror\mirror_app\`). The e2e tests run it |

## Input and output

**In:** `SmoothingInput` (`smoothing.h`):
- the followed frame's drawn pose
- the time since the last output frame
- the pipeline's previous output (`PreviousView`)
- the drawn pose of the frame shown with the view, which the view must stay inside. It is the
  frame's own, or an older frame's standing in on OBS ticks.
- the output's geometry
- the settings
- when the smoothing looks ahead: the head's path around the frame, with the drawn pose of the
  frame that will be shown at each of its moments. The moments lie a display period apart and the
  pipeline glue samples them around the frame's moment.

**Out** (`Smoothed`):
- the view (`Smooth`)
- whether the smoothing steered its path away from the edges
- whether its last resort stopped the view at an edge, and by how much. The glue counts the stops
  for the log, the trace and e2e.

**How it stays inside the image** is its own business, with the picture room's geometry
(`..\picture_room\README.md`):
- Instant asks the room of its previous view and catches up faster as it runs out.
- The look-ahead checks each moment of its solved path against the image shown at that moment
  (`Within`). Where the path would pass 90% of its room, it makes the margin point that moment's
  target, weighted 10,000 against the head's 1, and solves again, up to three passes. The jerk penalty
  eases the path out to the margin and back, ahead of time.
- Then each smoothing stops its view at the edge of the shown image if it is still past 95% of its
  room (`StopAtEdge`). Instant pulls its filter along at once, so it keeps no hidden lag behind the
  stop. The look-ahead keeps no state between frames.

## Implementations

| File | Class | Config value | When to use |
|---|---|---|---|
| `instant_smoothing.h/.cpp` | `InstantSmoothing` (with `OrientationFilter`) | `SmoothingKind::kInstant` (`smoothing` 1) | Live streaming, because it adds no delay. It is a causal two-stage One-Euro style filter with a dead zone and a soft lag limit (`kMinCutoffHz`, `kBeta`, `kDeadzoneDeg`, `kMaxAngleDeg`). It catches up faster as the view runs out of room, scaled to the head's speed (`kCatchUpHz`, `kCatchUpStart`) |
| `lookahead_smoothing.h/.cpp` | `LookaheadSmoothing` (with the path solver `LookaheadSmoother`) | `SmoothingKind::kLookahead` (`smoothing` 2, the default) | Recording. The view runs half a second behind the game. It comes from a jerk-penalised path over the head's poses around the frame, and the path's time constant adapts to the head's speed |

`ConfigFor(settings)` maps `smoothing` onto the implementation: 0 and 1 give Instant, and 2 gives
Look-ahead. At 0 the pipeline shows the headset's view as it is. Instant's tuning is its
own constants (`instant_smoothing.h`). `LookaheadFor` and `LookaheadOutputDelayS` stay inline in
`lookahead_smoothing.h` for e2e's runner, which links no core.

**Thread:** the host's output thread (OBS's graphics thread, the mirror app's main thread). Per-frame
allocation stops once the look-ahead's buffers have grown.

**Uses:** plain data and the picture room's geometry. It asks the picture room how much room
a view has.

**Must not know:** how the room is measured (it asks the picture room), frames' images, PVR or threads.
