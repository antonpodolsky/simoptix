# Terms

The terms of this repo, in one place, for the dev skill's files and as the source for every README's
terms table. `.agents\skills\docs\check_docs.py` reads the first column of the first two tables:
- **Terms coined in this repo.** A README that uses one has a row for it in its own terms table,
  before the first use. The script fails otherwise.
- **Outside terms.** Terms from Pimax, OpenXR or Windows that a reader may not know. A README defines
  one when its reader may not know it (the docs skill, "Who reads what"). The script lists the uses
  without a row, for review.

The last table holds everyday words that this repo uses in one narrow sense. They need no row in a
README, but they always mean what the table says.

A new term goes here, and into the table of every README that uses it.

## Terms coined in this repo

| Term | Meaning |
|---|---|
| game eye | The eye image a frame source delivers. The Pimax source's is a half-size copy of the left eye (2508×2084 in LMU at Render Quality 1.0). The OpenXR source's is the left eye at full size (5084×4168 in LMU) |
| frame source | The code that delivers game frames, head poses and timing to VR Mirror: the Pimax source (`vr_mirror\frame_sources\pimax\`) or the OpenXR source (`vr_mirror\frame_sources\openxr\`) |
| host | A program that runs VR Mirror's pipeline and shows its result: the OBS plugin or the mirror app |
| mirror app | `simoptix_vr_mirror.exe`, VR Mirror in a desktop window (`vr_mirror\mirror_app\`). The e2e tests run it |
| display slot | One of the regular moments, 90 per second, at which the headset shows a new image. The game draws each frame for one slot, with the head pose the runtime predicted for that slot |
| display grid | The regular schedule the display slots lie on: one every 11.1 ms at 90 Hz |
| arrival slot | The display slot a game frame is assigned from when it arrived, once the timing noise is removed. While the game keeps up, it is the slot the frame was drawn for |
| drawn slot | The display slot a frame source reports a frame was drawn for. The OpenXR source reports it. The Pimax source can't |
| slot lead | The runtime's next display slot minus the moment a frame arrived. It shifts when the game queues frames |
| game behind | The game's GPU has fallen behind and queues up to two frames. Each arrives one or two display slots after the slot it was drawn for |
| drawn pose | The head pose a game frame was drawn with. Stage 2 of the pipeline answers it for each frame, and the code is named after it |
| slot check | A way to find the drawn pose: it judges from thumbnails which display slot a frame was really drawn for |
| timing gate | The part of the slot check that lets a slot be judged only while the frames' timing shows a queue |
| re-time | What the slot check does when it assigns a frame a display slot other than its arrival slot |
| cockpit correction | A refinement of the drawn pose. It measures how the fixed cockpit moved between two frames and corrects the pose by what the head poses don't explain |
| look-ahead | The method behind the Look-ahead smoothing. The view runs half a second behind the game, and the smoothing plans a calm path from the head's movement in that time |
| image room | How far a view can turn away from the pose of an eye image before the output would look past the image's edge and show black |
| edge stop | The smoothing's last resort. It holds the view at 95% of its image room when the smoothing's own planning wasn't enough |
| output delay | How far VR Mirror's view runs behind the game: about 0.5 s with Look-ahead, none with Instant, and 0 while the source is hidden. The G-meter, race auto-record and OBS's audio follow it |
| observer | The debugging code beside the pipeline. It numbers the stamp and writes the frame trace |
| stamp | The frame number that VR Mirror draws as 15 small squares in the top-left corner of the view, when its Debug setting is on |
| frame trace | The CSV file that the Debug setting (or the mirror app's `SIMOPTIX_VR_MIRROR_TRACE`) writes, with one row per drawn frame: its poses and timing. With the Pimax source, Debug also writes a second file of head poses, predictions and arrivals. With the OpenXR source, the second file holds the frames the layer published and how the game handed their images over |
| fake runtime | The stand-in for Pimax's client library (`vr_mirror\e2e\fake_pvr\`) that the e2e tests use in place of a headset and a game |
| replay | A real session played back through the fake runtime: its head poses, the runtime's predictions, and every game frame at its real arrival time and with the slot it was really drawn for |
| stage replay | `stage_replay.exe`. It runs the pipeline's stages offline, without the mirror app, the fake runtime or the GPU |
| golden file | The file `stage_replay --golden` writes: every output of the pipeline over a fixed set of inputs, to compare byte for byte |
| data folder | The folder `SIMOPTIX_DATA` names, outside the repo, where analyses of recordings are kept |

## Outside terms

| Term | Meaning |
|---|---|
| PVR | Pimax's own name for its client interface: the client library `libPVRClient64.dll` that Pimax Play installs, and the table of functions it hands out. The project's code that wraps it is named after it |
| client mirror | A feature of the Pimax runtime. It gives other programs a half-size copy of the left eye image the game draws, and a signal for each new frame |
| pi_server | The Pimax runtime's compositor process (`pi_server.exe`). Every call through PVR goes to it |
| API layer | A DLL that the OpenXR loader puts between a game and the runtime. It sees every OpenXR call the game makes |
| performance counter | The Windows high-resolution clock (`QueryPerformanceCounter`). All times in the code are seconds on it |
| sRGB encoding | The usual way images store brightness, with a curve that gives dark tones more values. Maths on encoded values without undoing the curve gives wrong results. Pimax's client mirror encodes the eye image twice |

## Words with a narrow meaning

| Word | Meaning here |
|---|---|
| frame, game frame | One frame the game draws for the headset, and its record. It arrives at VR Mirror at some moment, and it was drawn for one display slot with one head pose |
| image, eye image | The image in a frame. An eye image is the image of one eye. VR Mirror uses the left eye |
| view | What VR Mirror shows. In the pipeline, the direction its output camera looks in, as a rotation, which the smoothing chooses |
| head pose | Where the headset points at a moment, as a rotation. VR Mirror stabilizes rotation only. Leaning stays in the view as the game drew it |
| tick | One time OBS draws a frame of its output |
| lag | How many display slots earlier than its arrival slot the game drew a frame. 0 is on time. A lag appears while the game is behind |
| thumbnail | A 64×32 colour image of the middle of an eye image. The slot check compares them |
| patch | A small square (32×32 texels) cut from an eye image. The cockpit correction measures how patches move, and the e2e runner reads them from the captured window |
| core | The logic every VR Mirror host shares (`vr_mirror\core\`) |
| pipeline, stage | The five stages in `vr_mirror\core\pipeline\` that turn a game frame into the stabilized view: arrival timing, drawn pose, frame picker, smoothing and warp. Each stage answers one question through a small interface, and its implementations can be swapped |
| arrival timing | Stage 1. It removes the timing noise from a frame's arrival and gives its arrival slot |
| frame picker | Stage 3. It picks which stored frame the host shows next, and when |
| smoothing | Stage 4, and the setting that picks how the view follows the head: Off, Instant or Look-ahead |
| warp | Stage 5. It re-projects the eye image to the view, on the GPU |
| e2e, scenario | The end-to-end tests in `vr_mirror\e2e\`. A scenario is one of them: a motion or a replay, with the limits it is graded on |
| telemetry | The readings a game publishes about the car (G-forces, speed, pedals, ABS, TC) and the session (practice or race, the start lights, the finish) |
| source time | The frame source's clock, which the pipeline runs on |
