# Picture room (beside the stages)

The picture room answers a question for the smoothings: how much room does a view have inside a
game frame's eye image before the output would look past the image's edge and show black? It also
stops a view at that edge. It is stateless geometry beside the stages, not a stage of its own. Each
smoothing calls it to keep its view inside the images.

## Terms

| Term | Meaning |
|---|---|
| drawn pose | The head pose a game frame was drawn with. Stage 2 of the pipeline answers it for each frame, and the code is named after it |
| look-ahead | The method behind Smooth and Smoothest. The view runs half a second or a second behind the game, and the smoothing plans a calm path from the head's movement in that time |
| tick | One time OBS draws a frame of its output |
| frame trace | The CSV file that the Debug setting (or the mirror app's `SIMOPTIX_VR_MIRROR_TRACE`) writes, with one row per drawn frame: its poses and timing. With the Pimax source, Debug also writes a second file of head poses, predictions and arrivals. With the OpenXR source, the second file holds the frames the layer published and how the game handed their images over |
| picture room | How far a view can turn away from the pose of an eye image before the output would look past the image's edge and show black |

## What it does

- **Room** (`RoomIn` for a frame's image, `Within` for one moment of a path, with a quick fit first)
  tells how far a view trails a drawn pose, and how far it could before the output looks past the
  image. Instant catches up faster as its room runs out. The look-ahead plans its path inside the
  room (`4_smoothing\README.md`).
- **The stop at the edge** (`StopAtEdge`) is each smoothing's last resort, for when its own means
  weren't enough. It holds the view at 95% of its room (`kStopShare`) in the image shown with it.
  That image is the frame's own or, on the OBS plugin's ticks, the game frame standing in for the
  tick's moment. The stand-in was drawn for a slightly different moment, about 22 ms later with the
  fixed delay, and is sometimes the previous frame again. The stop says whether it acted, how far it
  moved the view, and what share of the view's trailing angle it kept (1 when it didn't act). With
  that, Instant pulls its filter along at once.

**In:** a drawn pose (the shown frame's, or a path moment's), a view, and the output's geometry
(`ViewGeometry`, `pipeline_types.h`).

**Out:**
- `EdgeRoom`: how far the view trails the image's pose (`lag`), its room, and the direction
- whether a view is within a share of its room
- `EdgeStop`: the view to show, whether the stop acted, how far it moved the view, and the share of
  the view's trailing angle it kept (`keep`)

## Files

| File | What |
|---|---|
| `picture_room.h/.cpp` | `RoomIn`, `Within`, `StopAtEdge`, `TrailingView`, `EdgeRoom`, `EdgeStop`, `kStopShare` |

## How stops are counted

The glue counts the frames where a smoothing's stop acted (`Smoothed::stopped`,
`Pipeline::EdgeStops`). The count appears in four places:
- the OBS log's 10 s line "edge limit: N frames"
- the frame trace's `edge_clamps` and `edge_shift`
- e2e's "edge limit acted"
- stage_replay's "edge limit"

**Thread:** the output thread. The picture room holds no state.

**Uses:** plain geometry: poses, views and the output's shape.

**Must not know:** how the view is smoothed, frames and their timing, PVR or D3D.
