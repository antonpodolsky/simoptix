# Observer: debugging beside the pipeline

The observer is for debugging. It sits beside the stages and is not one of them. It only reads what the glue hands it. The pipeline glue (`Pipeline::Update`) hands it one record per output
frame, collected from the stages' answers. Whenever a host asks for the stages' periodic report
(`Pipeline::Report`), the glue also hands it every stage's numbers, which are the same lines the host
logs. An observer turns them into what helps, such as a trace row or the number the frame-number
stamp shows.

## Terms

| Term | Meaning |
|---|---|
| host | A program that runs VR Mirror's pipeline and shows its result: the OBS plugin or the mirror app |
| observer | The debugging code beside the pipeline. It numbers the stamp and writes the frame trace |
| stamp | The frame number that VR Mirror draws as 15 small squares in the top-left corner of the view, when its Debug setting is on |
| mirror app | `simoptix_vr_mirror.exe`, VR Mirror in a desktop window (`vr_mirror\mirror_app\`). The e2e tests run it |
| frame trace | The CSV file that the Debug setting (or the mirror app's `SIMOPTIX_VR_MIRROR_TRACE`) writes, with one row per drawn frame: its poses and timing. With the Pimax source, Debug also writes a second file of head poses, predictions and arrivals. With the OpenXR source, the second file holds the frames the layer published and how the game handed their images over |
| tick | One time OBS draws a frame of its output |
| display slot | One of the regular moments at which the headset shows a new image, one per refresh (90 per second at 90 Hz). The game draws each frame for one slot, with the head pose the runtime predicted for that slot |
| frame source | The code that delivers game frames, head poses and timing to VR Mirror: the Pimax source (`vr_mirror\frame_sources\pimax\`) or the OpenXR source (`vr_mirror\frame_sources\openxr\`) |
| drawn pose | The head pose a game frame was drawn with. Stage 2 of the pipeline answers it for each frame, and the code is named after it |
| slot check | A way to find the drawn pose: it judges from thumbnails which display slot a frame was really drawn for |
| cockpit correction | A refinement of the drawn pose. It measures how the fixed cockpit moved between two frames and corrects the pose by what the head poses don't explain |

A new method's numbers reach the trace through its `Report`, with no host changes. A new kind of
debugging, such as on-screen numbers, is a new observer.

## What the observer gets

`FrameRecord` (`observer.h`) holds:
- the game frame shown and its timing
- the pose its eye image was drawn with
- the pose the view followed
- the view
- the counters

Hosts only draw the stamp (`Pipeline::Stamp`), give the trace somewhere to write
(`Pipeline::SetTraceSink`), and say when they presented a frame (`Pipeline::Presented`).

## Implementations

| File | Class | Config value | When to use |
|---|---|---|---|
| `observer.h` | `Observer`, `FrameRecord`, `TraceSink` | none | The interface and its data |
| `no_observer.h` | `NoObserver` | `ObserverKind::kNone` (default) | No debugging |
| `stamp_trace_observer.h/.cpp` | `StampTraceObserver` | `ObserverKind::kStampTrace` | The OBS plugin's **Debug** setting and the mirror app's `SIMOPTIX_VR_MIRROR_TRACE`. It numbers each output frame for the stamp and writes the frame trace |

## The frame trace

`StampTraceObserver::kColumns` lists the columns. There is one row per output frame, written once
the frame was presented or the next one came. Times are in source seconds and quaternions are
x, y, z, w.

| Column | Meaning |
|---|---|
| `stamp` | The number the stamp shows (15 bits) |
| `tick` | The host's tick, or when it drew |
| `serial` | The game frame's serial number |
| `arrival` | When the frame arrived |
| `smooth` | The de-jittered arrival |
| `slot` | The display slot the frame arrived for |
| `img_*` | The pose the frame's eye image was drawn with |
| `raw_*` | The pose the view followed: the frame's, or a tick's own moment |
| `view_*` | The view |
| `drawn_pose` | How `img` was found |
| `draw` | When the host drew |
| `presented` | When the host presented (0 where the host doesn't present, as in OBS) |
| `zoom` | The zoom |
| `edge_clamps` | So far, the frames where the smoothing's last resort stopped the view at the edge of the shown image |
| `edge_planned` | So far, the frames whose smoothing steered its path |
| `slot_lead` | The runtime's next display slot minus the frame's arrival |
| `view_time` | The moment the view is for, on a tick's own timeline (0: the frame's own) |
| `followed_serial` | The game frame the view followed. It is `serial` itself unless the game sent no new frame in time and the last one is shown again |
| `edge_shift` | How far the smoothing's last resort moved this frame's view, in degrees (0 when it didn't act) |
| `aspect` | The output's width divided by its height |
| `drawn_slot` | The display slot the frame source says the frame was drawn for. It is the OpenXR source's, and 0 for the Pimax source, whose frames don't say |
| `pose_notes` | The drawn pose stage's own account of how it found this frame's pose (`DrawnPose::Describe`): space-separated `name=value` pairs in its own terms. Examples are the slot check's `slots_before_arrival=2 judged=1 own_judgement=1` and the cockpit correction's `looked_at=1 corrected=1 correction_deg=0.312` |

The stages' report lines follow as `#stats,<line>`.

The first 19 columns are the OBS plugin's frame trace as `vr_mirror\recording_analysis` reads it, by
position. The mirror app's trace has the same columns, and the e2e runner reads them by name.

**In:** `FrameRecord` per output frame, the report lines, and the host's sink and present times.
**Out:** `Stamp` and the trace's lines.

**Thread:** the host's output thread (OBS's graphics thread, the mirror app's main thread). Rows are
formatted on the stack and nothing is allocated per frame. The OBS plugin's sink is its `TraceWriter`, which
writes to disk on its own thread. The mirror app's sink is a file.

**Uses:** the frame records and report lines it is handed, and a sink to write to.

**Must not know:** files, D3D, OBS, or how any stage works.
