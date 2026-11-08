# Stage 5: warp

This stage turns the view chosen for an output frame into the numbers the host's shader needs to
draw it. The view's camera looks at the middle of the eye image, halfway between its edges, and is
levelled itself while the view is stabilized. For every output pixel, the shader rotates the ray of that
camera into the eye camera that the shown frame was drawn from, through the eye's 2° cant, and looks it
up in the eye image.
Where the ray falls outside the image, the shader draws black. The per-pixel half is `warp.hlsli`,
which each host wraps in its own shader (the mirror app) or effect (the OBS plugin).

## Terms

| Term | Meaning |
|---|---|
| drawn pose | The head pose a game frame was drawn with. Stage 2 of the pipeline answers it for each frame, and the code is named after it |
| game eye | The eye image a frame source delivers. The Pimax source's is a half-size copy of the left eye (2508×2084 in LMU at Render Quality 1.0). The OpenXR source's is the left eye at full size (5084×4168) |
| frame source | The code that delivers game frames, head poses and timing to VR Mirror: the Pimax source (`vr_mirror\frame_sources\pimax\`) or the OpenXR source (`vr_mirror\frame_sources\openxr\`) |
| host | A program that runs VR Mirror's pipeline and shows its result: the OBS plugin or the mirror app |
| mirror app | `simoptix_vr_mirror.exe`, VR Mirror in a desktop window (`vr_mirror\mirror_app\`). The e2e tests run it |

## Input and output

**In:** the head pose the shown frame was drawn with (its drawn pose), the view to show (both
quaternions) and the output's geometry (`ViewGeometry`, `pipeline_types.h`).

The frame source says how the game eye is encoded. If it is sRGB-encoded twice (the Pimax game eye,
`EyeInfo::encodedTwice`), the warp decodes one encoding. If it is encoded once (the OpenXR layer's),
the warp shows it as it is.

**Out:** `WarpParams`, seven float4s laid out as `warp.hlsli` expects.

## Implementations

| File | Class | Config value | When to use |
|---|---|---|---|
| `rotation_warp.h/.cpp` | `RotationWarp` | `WarpKind::kRotation` | Always. It re-projects for a change of head orientation only. The scene is far enough away that the head's position doesn't matter |
| `warp.hlsli` | none | none | The GPU half. `vr_mirror\core\build\embed_text.ps1` embeds it into each host |

The eye image is warped from the pose the game drew it with, not from the measured head.

**Thread:** the host's output thread (OBS's graphics thread, the mirror app's main thread). The stage
holds no state.

**Uses:** the pose, the view and the output geometry as plain data.

**Must not know:** how the view was chosen (smoothing, edges), frames and their timing, D3D, OBS or the window.
