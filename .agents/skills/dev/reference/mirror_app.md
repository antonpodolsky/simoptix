# The mirror app: building it, and its use in the tests

Terms: `reference\terms.md`. What the app is, its settings and its files: `vr_mirror\mirror_app\README.md`.

## Build

Run `vr_mirror\mirror_app\build.bat` (Visual Studio 2019 or later, with the C++ tools).
- It builds the window host together with the Pimax and OpenXR frame sources' readers and the core,
  and embeds `vr_mirror\core\pipeline\5_warp\warp.hlsli` into the exe.
- With an exe name as its argument (`build.bat simoptix_vr_mirror_dev.exe`), it builds a second copy.
  Use that while `simoptix_vr_mirror.exe` is running, because Windows holds a running exe.

## In the tests

For each scenario, the e2e runner:
1. copies the built exe next to the fake runtime
2. writes its ini: the scenario's `smoothing`, `zoom`, `drawn_pose` and window size
3. sets `SIMOPTIX_VR_MIRROR_TRACE` (the frame trace the runner grades) and
   `SIMOPTIX_VR_MIRROR_SERVER_EXE` (the fake's process name)

The pipeline's observer writes the trace in the OBS plugin's columns
(`vr_mirror\core\pipeline\observer\README.md`), and the runner reads them by name. In older builds the
mirror app had columns of its own, with the frame's lag (`warped_slot`, `decided_lag`) and the cockpit
correction (`corr_*`).

The app draws once per game frame. Drawing on its own clock, like the OBS plugin, was tried and
dropped (`findings\e2e.md`, "Drawing on ticks").
