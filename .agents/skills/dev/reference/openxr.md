# The OpenXR layer: build, check, install, and the Easy Anti-Cheat test

Terms: `reference\terms.md`. What the layer is and does: `vr_mirror\frame_sources\openxr\README.md`.
What each start of the game showed: `findings\openxr.md`.

## Build and check

```
vr_mirror\frame_sources\openxr\build.bat
```

```
"vr_mirror\frame_sources\openxr\bin\check\Le Mans Ultimate.exe"
```

The check program runs the freshly built layer the way the game will, before the layer goes anywhere
near the game.
- It loads a real OpenXR loader (SteamVR's, or the one `--loader` names), and switches the layer on for
  itself only.
- It behaves like LMU: the same name and extensions, a D3D11 session, one image buffer per eye in the
  game's format and usage, and five seconds of frames. It clears the left eye to a colour that encodes
  the frame's number.
- Meanwhile it reads what the layer publishes, as VR Mirror does, on a device of its own.
- It fails unless:
  - every frame record holds what it submitted
  - nearly every frame after the first copy was copied
  - every copy read back shows its own frame
  - the layer's clock reading matches the runtime's own conversion
- It carries the game's file name only because the layer wakes up for no other name. It touches no game
  file.
- It needs the headset on, with Pimax Play running. Without them it stops at `xrGetSystem` (−35, no
  headset available) and says so. The log then shows only the handshake and the instance.

Afterwards the layer's log (`%LOCALAPPDATA%\simoptix\openxr_frame_source.log`) should show the runtime,
the time conversion asked for, the session, both buffers, the frame layout, the frame source's lines,
and no errors. The copy time it logs is not the game's: an image cleared to one colour is stored
compressed, and copies much faster than a drawn one.

An installed copy of the layer wakes up in the check program too, because the program is called
`Le Mans Ultimate.exe`. So the check switches off an installed copy of the same name for itself
(`DISABLE_XR_APILAYER_SIMOPTIX_frame_source`).

## Install for the game

Close LMU and double-click `vr_mirror\frame_sources\openxr\install.bat`.
1. It builds the layer first, so it never installs an old build. The build is unsigned, because the
   repo holds no signing tools ("The Easy Anti-Cheat test", below).
2. It asks Windows for administrator rights.
3. It runs `install.ps1` in a window that stays open, so the result can be read.

`install.ps1` copies the DLL and its manifest to `C:\Program Files\simoptix\openxr_frame_source\`, and
registers it in `HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit` (value 0 = on, 1 = off).
`uninstall.bat` and `uninstall.ps1` remove both, the same way.

By hand, in PowerShell as administrator, with the full path, because an administrator window starts in
`C:\Windows\system32`:

```
powershell -ExecutionPolicy Bypass -File "<repo>\vr_mirror\frame_sources\openxr\install.ps1"
```

## Starting LMU without Easy Anti-Cheat

Easy Anti-Cheat refuses the unsigned layer (`findings\openxr.md`). So the layer runs in LMU started
without it, the way the LMU quad-view mod's offline mode does. Online play is disabled in that game.

1. Start Steam and log in. Started without Steam, the game hands its start to Steam, which goes
   through Easy Anti-Cheat.
2. Run `Le Mans Ultimate.exe +XR` from the game's folder, with that folder as the working directory.

To compare with a start without the layer, set `DISABLE_XR_APILAYER_SIMOPTIX_frame_source=1` for that
start.

## The Easy Anti-Cheat test

Unsigned, or signed with a certificate Windows doesn't trust, the layer is kept out by Easy
Anti-Cheat (`findings\openxr.md`). The repo builds it unsigned and holds no signing tools. Signing with
a trusted certificate happens outside the repo.

Test a signed DLL cheapest first, and stop at the first step where the game doesn't start:
1. **Offline.** Install it, start LMU the usual way, stay out of online sessions, and drive a few laps.
   The layer loads whenever LMU runs in OpenXR mode, however it is started. Write down whether the game
   starts, any Easy Anti-Cheat message, and the layer's log.
2. **Online.** The same, in an online session. This risks the LMU account, so do it only after step 1
   went cleanly.

Until then the layer and the OpenXR source are built and tested for offline use (`SKILL.md`, "Rules",
"Never touch the game").
