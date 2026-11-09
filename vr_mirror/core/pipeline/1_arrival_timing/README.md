# Stage 1: arrival timing

This stage removes the timing noise from each game frame's arrival and gives the display slot the
frame was drawn for. Frames arrive at a steady offset from the headset's 90 Hz display grid, give or
take about a millisecond of polling and GPU timing noise. Every later step uses the arrival time
with the noise removed.

## Terms

| Term | Meaning |
|---|---|
| arrival slot | The display slot a game frame is assigned from when it arrived, once the timing noise is removed. While the game keeps up, it is the slot the frame was drawn for |
| client mirror | A feature of the Pimax runtime. It gives other programs a half-size copy of the left eye image the game draws, and a signal for each new frame |
| frame source | The code that delivers game frames, head poses and timing to VR Mirror: the Pimax source (`vr_mirror\frame_sources\pimax\`) or the OpenXR source (`vr_mirror\frame_sources\openxr\`) |
| display slot | One of the regular moments, 90 per second, at which the headset shows a new image. The game draws each frame for one slot, with the head pose the runtime predicted for that slot |
| frame trace | The CSV file that the Debug setting (or the mirror app's `SIMOPTIX_VR_MIRROR_TRACE`) writes, with one row per drawn frame: its poses and timing. With the Pimax source, Debug also writes a second file of head poses, predictions and arrivals |
| host | A program that runs VR Mirror's pipeline and shows its result: the OBS plugin or the mirror app |
| lag | How many display slots earlier than its arrival slot the game drew a frame. 0 is on time. A lag appears while the game is behind |

## What it does

The stage gives the arrival slot in one of two ways:
- It finds the slot from the arrival time while the game keeps up (the Pimax source).
- It takes the slot the frame source reports (the OpenXR source).

## Input and output

**In:** `ArrivalInput` (`pipeline_types.h`), on the frame source's clock:
- the frame's arrival
- what the frame source knows of its slot: the runtime's next display slot at that moment, and the slot
  the source says the frame was drawn for (each 0 when unknown)
- the display period

**Out:** `ArrivalResult` (`pipeline_types.h`):
- the arrival with the noise removed (`smooth`)
- the display slot (`slot`)
- the learnt offset from arrival to slot (`offset`)

The frame source writes them into the frame's record (`GameFrame::smoothTime`, `slotTime`). The Pimax
source also writes them into its frame trace (F rows).

## Implementations

| File | Class | Config value | When to use |
|---|---|---|---|
| `grid_arrival_timing.h/.cpp` | `GridArrivalTiming` | `ArrivalTimingKind::kGrid` | A frame source whose frames carry only their arrival (the Pimax source). It takes a running median of the offset over half a second of arrivals, which resists odd readings. It adds the offset to the arrival and snaps the sum to the nearest slot on the grid (or to the slot before it for a single late frame, see below). Then it takes the offset off again |
| `reported_arrival_timing.h/.cpp` | `ReportedArrivalTiming` | `ArrivalTimingKind::kReported` | A frame source whose frames report the slot they were drawn for (the OpenXR source). It finds the arrivals' phase on the grid with a running circular median over half a second. Each arrival keeps its whole periods. The slot a frame is shown in is the slot it was drawn for when it came on time, and a later one when it came late. The largest gap from arrival to drawn-for slot over two seconds counts as the on-time one. A frame never gets the previous frame's slot. The drawn-for slot itself is in the frame's record (`GameFrame::drawnSlot`) |

`ConfigFor` picks the implementation from what the frame source reports (`FrameReport`). The host
makes it with `MakeArrivalTiming(config.arrivalTiming)` and hands it to the frame source before the
source starts (`PimaxSource::SetArrivalTiming`, `OpenXrSource::SetArrivalTiming`).

**Thread:** the frame source's own (the Pimax source's poll thread, the OpenXR source's reader
thread), as each frame arrives. The stage resets when the source goes idle, so the next game finds
its own offset, and when the session is re-created.

**Uses:** plain data: arrival times, slots and the display period.

**Must not know:** PVR calls, threads, the frame's image, or anything later in the pipeline.

## A single late frame takes the free slot before

Rounding to the nearest slot would give a frame that arrived more than half a period late the next
slot. The frame after it then takes that slot too. That puts two frames on one slot, lines the first
one up from the next slot's head pose, and causes a one-frame jolt.

So a frame that falls more than 0.2 periods (2.2 ms) before its nearest slot gets the slot before,
when both conditions hold:
- that slot is still free (the previous frame is on an earlier one)
- the previous frame was on time (within 0.15 periods of its slot)

A late frame right after another late frame keeps the rounded slot on purpose. In a queue, the drawn
pose stage counts its lag from this stage's slot.

The noise this stage removes is logged every 12 s ("arrival timing noise removed").
