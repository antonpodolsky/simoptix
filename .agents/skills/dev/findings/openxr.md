# Findings: the OpenXR layer

Terms: `reference\terms.md`. What the layer does: `vr_mirror\frame_sources\openxr\README.md`. How to
build, check and install it, and the Easy Anti-Cheat test plan: `reference\openxr.md`.

This file holds what each start of the game and of the check program showed, in order. File names are
as they were then: the load log was `openxr_layer_loads.log`, and the DLL
`XR_APILAYER_SIMOPTIX_frame_tap.dll` in `Program Files\simoptix\openxr_layer\`.

## The test machine

- **Another API layer already loads into LMU** (2026-11-04). OpenXR Toolkit is installed (registered in
  HKLM, switched on), and its log shows it active in LMU's 2-hour session that day.
  - Its DLL's certificate expired on 2024-02-27, and the signature has no timestamp. So Windows reports
    the signature as not valid.
  - Whether that session was online is not known.
- **The game's image buffers**, from that OpenXR Toolkit log: two, one per eye, 5084×4168 each, format
  29 (`R8G8B8A8_UNORM_SRGB`), drawn with Direct3D 11 on Pimax OpenXR 0.1.0.

## The check program

- **2026-11-06, no headset.** The layer loaded through SteamVR's OpenXR loader (1.1.62), joined the
  chain and reached the Pimax runtime.
  - OpenXR Toolkit sat before it in the chain and added four extensions to the instance.
  - Switched on for one program, the layer counts as "explicit". In the game both layers are
    "implicit", and their order there may differ.
  - Under another program name, the layer passed everything through and wrote nothing.
  - Sessions and frames couldn't be checked: the runtime reported no headset (error −35).
- **2026-11-06, with the headset.** The check passed with the layer in the chain: a D3D11 session on
  the Pimax Dream Air, two 5084×4168 buffers in format 29, 1,100 frames, and a clean shutdown.
  - The layer logged the frame layout once, and "900 frames in 10.0 s, 89.97 per second", with nothing
    skipped.
  - OpenXR Toolkit added a third buffer of its own, 2048×2048.
- **2026-11-07 20:11, the copy** (Pimax OpenXR 0.1.0, Dream Air, 1,100 frames). Copying the left eye
  after `xrEndFrame` has gone on to the runtime works on Pimax's runtime.
  - Every record held what was submitted.
  - The reader appeared, and the textures were made 0.3 s after the session began, in 0.5 ms. From
    then all 1,073 frames were copied. All 1,072 read back by a second device showed their own frame's
    image. The last one's marker was never checked.
  - The layer's clock reading equals the runtime's own conversion to the nanosecond.
  - 89.97 frames per second, as without the copy.
  - The check's instance asked only for D3D11, so the layer added the time conversion itself. OpenXR
    Toolkit, before it in the chain, adds it too.
  - The copy took 0.046 ms on the GPU (p50, p99 0.047, max 0.59). That is far less than 85 MB should
    take, because the check clears each image to one colour, which the card stores compressed. The
    copy's time in the game is still to be measured.
  - The shared textures were made as `R8G8B8A8_TYPELESS` (format 27).

## LMU started through Steam, with Easy Anti-Cheat

- **2026-11-06 20:35, the first start with the unsigned layer installed.** The game started normally.
  Easy Anti-Cheat's log says only "successfully loaded in-game", with no mention of any DLL. OpenXR
  Toolkit loaded into LMU (its log).
  - The layer wrote no log. So either the DLL was never loaded, or the handshake failed before the log
    started, because that version logged nothing then.
  - The load note and the handshake log were added, to tell the two apart at the next start.
- **20:39, the second start, with the load note.** No line from LMU in the load log, so Windows never
  loaded the DLL into the game. OpenXR Toolkit loaded again.
  - The same installed copy, found through the registry as the game finds it (`--installed`), loads
    and runs in the check program with three OpenXR loaders: 1.0.6 (OpenXR Toolkit's), 1.0.25
    (Pimax's) and 1.1.62 (SteamVR's).
  - So the manifest and the registration are fine, and Easy Anti-Cheat is the likely reason. It keeps
    the unsigned DLL out, and lets OpenXR Toolkit's signed one in (its certificate expired).
  - The game itself runs normally either way.
- **Easy Anti-Cheat names and refuses the unsigned DLL.** Windows' system log (event 26) holds an Easy
  Anti-Cheat popup "Untrusted system file
  (…\Program Files\simoptix\openxr_layer\XR_APILAYER_SIMOPTIX_frame_tap.dll)" at 20:40:35, 20:56:02 and
  20:58:38, in the starts through Steam.
  - Each time, Easy Anti-Cheat's driver unloaded a second later.
  - In the game that ran from 20:58:40, after that popup, online play was disabled, and the layer
    wasn't loaded either. Why not is not known.
  - So the next step was a signed DLL.
- **21:26, signed with a self-signed test certificate.** The certificate was in the personal store
  only, and not trusted by Windows ("terminated in a root certificate which is not trusted").
  - LMU started normally through Steam with Easy Anti-Cheat. The game ran and OpenXR Toolkit loaded,
    but **the DLL was still not loaded** (no line in the load log).
  - Unlike with the unsigned DLL, there was no "Untrusted system file" popup, also not when the game
    closed (the driver unloaded at 21:28:33).
  - So a signature alone, without a certificate Windows trusts, does not get the layer into the game.
  - Whether a trusted certificate does was tested on 2026-11-09 (next finding).
- **2026-11-09, signed with Certum's open-source code signing certificate.** Windows trusts it.
  - The key is RSA 3072 and stays in Certum's cloud (SimplySign). The DLL was signed with SHA-256 and
    a Certum timestamp. Windows reports the signature as valid, with the chain up to Certum Trusted
    Network CA.
  - It was installed with `install.ps1` directly, because `install.bat` builds first and would
    install an unsigned DLL.
  - LMU started twice through Steam with Easy Anti-Cheat. Its launcher logged "Easy Anti-Cheat
    successfully loaded in-game", and Windows logged no "Untrusted system file" popup.
  - **The layer loaded and worked in the game both times.** VR Mirror's OBS log showed "the layer
    runs in the game", the layer's textures at 5084×4168, and 899–901 frames per 10 s with their
    image.
  - Online play stayed enabled.
  - The layer wrote no log in either start: neither `openxr_frame_source.log` nor
    `openxr_frame_source_loads.log` in `%LOCALAPPDATA%\simoptix\` changed. Why not is not known.
  - Not tested: an online session (step 2 of the test in `reference\openxr.md`).

## LMU started without Easy Anti-Cheat

- **Steam's launch options for LMU** (Steam's app info) are plain, `+VR` and `+XR`. All go through
  `start_protected_game.exe`, which starts Easy Anti-Cheat. None starts the game without it.
- **Starting LMU without Easy Anti-Cheat:** run `Le Mans Ultimate.exe +XR` from the game folder. This
  is how the LMU quad-view mod does its offline mode, in its `mod/launch.ps1`.
  - Started that way from PowerShell at 20:47, with Steam open, the game ran without Easy Anti-Cheat,
    and **the layer loaded and worked in the real game**.
  - It saw two 5084×4168 buffers in format 29 (usage 0x11: the game copies its image in), one
    projection layer and no depth.
  - The field of view per eye was 52.6° outward, 47.6° inward, and 44.6° up and down.
- **With Steam closed,** the game instead starts Steam and asks it to launch it
  (`steam://run/2399420//+XR`). That shows Steam's launch-option window and goes through Easy
  Anti-Cheat. At 20:58 that also happened from a desktop shortcut with Steam open. Why is not known.
- **The 13 fps seen in these tests came from Pimax, not from the game or the layer.** Pimax's server
  logged `standby mode enter.` at 20:46:08, with no game running. From then on it held its compositor
  at 12.857 frames per second for every app (78 of 90 refreshes missed, GPU 1–8% busy), until Pimax
  Play was restarted at 20:57:56. The layer's 12.86 per second at 20:47 was that rate.
- **21:10, with Pimax running normally.** Steam started first and logged in. Then
  `Le Mans Ultimate.exe +XR` started from the game folder, as the quad-view mod's launcher does. Easy
  Anti-Cheat's service stayed stopped, and there was no popup.
  - The layer loaded and counted 900, 899 and 901 frames in the 10 s periods after loading, 89.9–90.0
    per second.
  - Pimax's log for the same game says 89.97–90.07 per second, with nothing missed or stale.
  - Measured in the game's menu, not on track.
